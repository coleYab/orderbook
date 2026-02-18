#include "orderbook/matching_engine.h"
#include "orderbook/terminal.h"
#include "orderbook/wal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct StockConfig { StockId id; std::string_view name; Price baseFairValue; };

constexpr StockConfig kStocks[] = {
    {1, "AAPL", 10000},
    {2, "GOOG", 15000},
    {3, "MSFT", 12000},
};

constexpr std::size_t kNumStocks = std::size(kStocks);
constexpr std::size_t kMaxEvents = 10;
constexpr std::size_t kChartDepth = 8;

class TradeStats {
    Quantity totalVolume_{0};
    double volumePriceSum_{0.0};
    std::uint64_t tradeCount_{0};
public:
    void RecordTrade(Price p, Quantity q) {
        totalVolume_ += q;
        volumePriceSum_ += static_cast<double>(p) * static_cast<double>(q);
        ++tradeCount_;
    }
    Quantity     GetTotalVolume() const { return totalVolume_; }
    double       GetVWAP() const { return totalVolume_ ? volumePriceSum_ / static_cast<double>(totalVolume_) : 0.0; }
    std::uint64_t GetTradeCount() const { return tradeCount_; }
};

class Simulator {
public:
    Simulator(MatchingEngine engine, OrderId nextOrderId,
              std::string walPath, bool useWal);
    ~Simulator();
    void Run();

private:
    struct StockContext {
        StockId id;
        std::string_view name;
        Price fairValue;
        TradeStats stats;
        std::size_t ordersPlaced{0};
    };

    struct EventEntry {
        std::chrono::system_clock::time_point time;
        std::string_view stockName;
        std::string_view type;
        std::string text;
        std::string detail;
    };

    std::chrono::steady_clock::time_point startTime_;
    MatchingEngine engine_;
    std::vector<StockContext> stocks_;
    std::atomic<bool> running_{true};
    std::atomic<bool> paused_{false};
    std::thread simThread_;
    std::atomic<OrderId> nextOrderId_{1};
    std::size_t selectedStock_{0};
    std::mt19937 rng_;
    std::mutex logMutex_;
    std::deque<EventEntry> eventLog_;
    std::string walPath_;
    bool useWal_;

    void SimulationLoop();
    void PlaceRandomOrder(StockContext& stock);
    void LogEvent(std::string_view stockName, std::string_view type,
                  std::string text, std::string detail = {});
    void Render(const Terminal& term);
    void HandleKey(int key);
    void RestartFromWal();
    static void SetupStockCallbacks(Simulator& sim);
};

Simulator::Simulator(MatchingEngine engine, OrderId nextOrderId,
                     std::string walPath, bool useWal)
    : startTime_(std::chrono::steady_clock::now())
    , engine_(std::move(engine))
    , nextOrderId_{nextOrderId}
    , rng_(std::random_device{}())
    , walPath_{std::move(walPath)}
    , useWal_{useWal}
{
    stocks_.reserve(kNumStocks);
    for (const auto& cfg : kStocks) {
        StockContext sc;
        sc.id = cfg.id;
        sc.name = cfg.name;
        sc.fairValue = cfg.baseFairValue;
        stocks_.push_back(std::move(sc));
    }
    SetupStockCallbacks(*this);
}

Simulator::~Simulator() {
    running_ = false;
    if (simThread_.joinable()) simThread_.join();
}

void Simulator::SetupStockCallbacks(Simulator& sim) {
    for (auto& sc : sim.stocks_) {
        auto stock = sim.engine_.EnsureStock(sc.id);
        stock->SetEventCallback([&sim, &sc](const OrderBookEvent& event) {
            std::string_view type;
            std::string text;
            std::string detail;

            std::visit([&](const auto& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, TradeEvent>) {
                    type = "TRADE";
                    text = std::format("@ {} x {}", e.price_, e.quantity_);
                    sc.stats.RecordTrade(e.price_, e.quantity_);
                }
                else if constexpr (std::is_same_v<T, OrderAddedEvent>) {
                    type = "ADDED";
                    char s = (e.order_->GetSide() == Side::Buy) ? 'B' : 'S';
                    if (e.order_->IsIceberg())
                        text = std::format("{} {} x {} (iceberg pk={})", s,
                            e.order_->GetPrice(), e.order_->GetInitialQuantity(), e.order_->GetPeakSize());
                    else
                        text = std::format("{} {} x {}", s, e.order_->GetPrice(), e.order_->GetRemainingQuantity());
                }
                else if constexpr (std::is_same_v<T, OrderFilledEvent>) {
                    type = "FILLED";
                    char s = (e.order_->GetSide() == Side::Buy) ? 'B' : 'S';
                    text = std::format("{} total {}", s, e.order_->GetFilledQuantity());
                }
                else if constexpr (std::is_same_v<T, OrderCancelledEvent>) {
                    type = "CANCEL";
                    char s = (e.order_->GetSide() == Side::Buy) ? 'B' : 'S';
                    text = std::format("{} rem {}", s, e.order_->GetRemainingQuantity());
                }
                else if constexpr (std::is_same_v<T, OrderRejectedEvent>) {
                    type = "REJECT";
                    char s = (e.order_->GetSide() == Side::Buy) ? 'B' : 'S';
                    text = std::format("{} {}", s, e.order_->GetPrice());
                    detail = e.reason_ ? e.reason_ : "unknown";
                }
                else if constexpr (std::is_same_v<T, OrderExpiredEvent>) {
                    type = "EXPIRED";
                    char s = (e.order_->GetSide() == Side::Buy) ? 'B' : 'S';
                    text = std::format("{} {}", s, e.order_->GetPrice());
                }
                else if constexpr (std::is_same_v<T, StopPlacedEvent>) {
                    type = "STOP";
                    char s = (e.order_->GetSide() == Side::Buy) ? 'B' : 'S';
                    text = std::format("{} trig={} lmt={}", s, e.order_->GetStopPrice(), e.order_->GetPrice());
                }
                else if constexpr (std::is_same_v<T, StopTriggeredEvent>) {
                    type = "TRIGGER";
                    text = std::format("active @ {}", e.triggerOrder_->GetPrice());
                }
            }, event);

            std::lock_guard lock(sim.logMutex_);
            sim.eventLog_.push_back({std::chrono::system_clock::now(), sc.name, type, std::move(text), std::move(detail)});
            if (sim.eventLog_.size() > kMaxEvents) sim.eventLog_.pop_front();
        });
    }
}

void Simulator::LogEvent(std::string_view stockName, std::string_view type,
                         std::string text, std::string detail)
{
    std::lock_guard lock(logMutex_);
    eventLog_.push_back({std::chrono::system_clock::now(), stockName, type, std::move(text), std::move(detail)});
    if (eventLog_.size() > kMaxEvents) eventLog_.pop_front();
}

void Simulator::SimulationLoop() {
    std::uniform_int_distribution<std::size_t> stockIdx(0, kNumStocks - 1);
    std::uniform_int_distribution<int> delayDist(10, 150);
    while (running_.load()) {
        if (!paused_.load()) {
            auto& sc = stocks_[stockIdx(rng_)];
            PlaceRandomOrder(sc);
            ++sc.ordersPlaced;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayDist(rng_)));
    }
}

void Simulator::PlaceRandomOrder(StockContext& stock) {
    std::uniform_int_distribution<int> typeRoll(0, 99);
    std::uniform_int_distribution<Quantity> qtyLow(10, 200);
    std::uniform_int_distribution<Quantity> qtyMed(200, 1000);
    std::uniform_int_distribution<Quantity> qtyBig(1000, 5000);
    std::uniform_int_distribution<Price> offsetDist(1, 40);
    std::uniform_int_distribution<Price> aggressiveOff(0, 10);
    std::bernoulli_distribution sideCoin(0.5);

    int roll = typeRoll(rng_);
    OrderType type = OrderType::Limit;
    TimeInForce tif = TimeInForce::GoodTillCancel;
    Side side = sideCoin(rng_) ? Side::Buy : Side::Sell;
    Price price = 0;
    Price stopPrice = 0;
    Quantity peakSize = 0;
    Quantity qty = qtyMed(rng_);

    if (roll < 40) {
        type = OrderType::Limit; tif = TimeInForce::GoodTillCancel; qty = qtyMed(rng_);
        price = (side == Side::Buy) ? stock.fairValue - offsetDist(rng_) : stock.fairValue + offsetDist(rng_);
    } else if (roll < 55) {
        type = OrderType::Market; tif = TimeInForce::ImmediateOrCancel; qty = qtyLow(rng_);
    } else if (roll < 70) {
        type = OrderType::Limit; tif = TimeInForce::ImmediateOrCancel; qty = qtyLow(rng_);
        price = (side == Side::Buy) ? stock.fairValue + aggressiveOff(rng_) : stock.fairValue - aggressiveOff(rng_);
    } else if (roll < 82) {
        type = OrderType::StopLimit; tif = TimeInForce::GoodTillCancel; qty = qtyMed(rng_);
        stopPrice = (side == Side::Buy) ? stock.fairValue + offsetDist(rng_) * 2 : stock.fairValue - offsetDist(rng_) * 2;
        price = stopPrice + ((side == Side::Buy) ? -Price{5} : Price{5});
    } else if (roll < 92) {
        type = OrderType::Limit; tif = TimeInForce::FillOrKill; qty = qtyBig(rng_);
        price = (side == Side::Buy) ? stock.fairValue + offsetDist(rng_) : stock.fairValue - offsetDist(rng_);
    } else {
        type = OrderType::Limit; tif = TimeInForce::GoodTillCancel; qty = qtyBig(rng_);
        peakSize = std::max(Quantity{10}, qty / 5);
        price = (side == Side::Buy) ? stock.fairValue - offsetDist(rng_) : stock.fairValue + offsetDist(rng_);
    }

    auto order = std::make_shared<Order>(type, tif, nextOrderId_.fetch_add(1), side, price, qty, stopPrice, peakSize);
    LogEvent(stock.name, "PLACE", std::format("{} {} {} {}",
        (side == Side::Buy) ? 'B' : 'S', OrderTypeLabel(type), price, qty));
    engine_.PlaceOrder(stock.id, std::move(order));

    std::normal_distribution<double> drift(0.0, 3.0);
    stock.fairValue = std::clamp<Price>(stock.fairValue + static_cast<Price>(std::round(drift(rng_))), 100, 999000);
}

void Simulator::Render(const Terminal& term) {
    int tw = term.Width();

    std::ostringstream out;
    out << "\033[2J\033[H\033[?25l";

    auto padLine = [&](const std::string& content) {
        int clen = VisibleLen(content);
        out << content;
        if (clen < tw) out << std::string(static_cast<std::size_t>(tw - clen), ' ');
        out << style::R << "\n";
    };

    {
        out << style::G7 << "\u250c" << style::R;
        out << style::B << style::BC << " \u25c6 DEPTH CHART " << style::R;
        out << style::G7 << "\u2500" << style::R << "  ";
        if (paused_.load())
            out << style::B << style::BY << "\u25a0 PAUSED " << style::R;
        else
            out << style::B << style::BG << "\u25b8 RUNNING " << style::R;
        out << style::G7 << "\u2500" << style::R;
        out << "  " << style::B << style::BW << Elapsed(startTime_) << style::R;

        for (std::size_t i = 0; i < kNumStocks; ++i) {
            out << "  ";
            if (i == selectedStock_)
                out << style::B << style::BG << "\u25c6" << stocks_[i].name << style::R;
            else
                out << style::D << (i + 1) << stocks_[i].name << style::R;
        }

        int hlen = VisibleLen(out.str());
        out << style::G7;
        while (hlen < tw - 1) { out << "\u2500"; ++hlen; }
        out << "\u2510" << style::R << "\n";
    }

    const auto& sc = stocks_[selectedStock_];
    auto levels = engine_.GetOrderBookLevelInfos(sc.id);

    std::optional<Price> bestBid, bestAsk;
    Quantity bidSz = 0, askSz = 0;
    if (levels) {
        const auto& bids = levels->GetBids();
        const auto& asks = levels->GetAsks();
        if (!bids.empty()) { bestBid = bids[0].price_; bidSz = bids[0].quantity_; }
        if (!asks.empty()) { bestAsk = asks[0].price_; askSz = asks[0].quantity_; }
    }
    Price spread = (bestBid && bestAsk) ? *bestAsk - *bestBid : 0;
    Price mid    = (bestBid && bestAsk) ? (*bestBid + *bestAsk) / 2 : 0;
    const auto& stats = sc.stats;

    {
        out << style::G7 << "\u2502" << style::R << "  ";
        out << style::B << style::BY << sc.name << style::R << " #" << sc.id;
        if (bestBid && bestAsk) {
            out << "    " << style::G << "Bid " << Comma(*bestBid) << " x " << CompactQty(bidSz) << style::R;
            out << "    " << style::R_ << "Ask " << Comma(*bestAsk) << " x " << CompactQty(askSz) << style::R;
            out << "    " << style::C << "Spr " << spread << "  Mid " << Comma(mid) << style::R;
        } else if (bestBid) {
            out << "    " << style::G << "Bid " << Comma(*bestBid) << " x " << CompactQty(bidSz) << style::R;
            out << "    " << style::G7 << "(no asks)" << style::R;
        } else if (bestAsk) {
            out << "    " << style::G7 << "(no bids)" << style::R;
            out << "    " << style::R_ << "Ask " << Comma(*bestAsk) << " x " << CompactQty(askSz) << style::R;
        } else {
            out << "    " << style::G7 << "(empty book)" << style::R;
        }
        padLine("");
    }

    {
        out << style::G7 << "\u2502" << style::R;
        out << "   " << style::D << "Trades:" << style::R << " " << stats.GetTradeCount()
            << style::D << "  Volume:" << style::R << " " << Comma(stats.GetTotalVolume())
            << style::D << "  VWAP:" << style::R << " " << std::format("{:.1f}", stats.GetVWAP());
        if (bestBid && bestAsk)
            out << style::D << "  Spread:" << style::R << " " << spread
                << style::D << "  Mid:" << style::R << " " << Comma(mid);
        padLine("");
    }

    if (levels && (!levels->GetBids().empty() || !levels->GetAsks().empty())) {
        const auto& bids = levels->GetBids();
        const auto& asks = levels->GetAsks();

        struct CumLevel { Price price; Quantity qty; Quantity cum; };
        std::vector<CumLevel> bidCum, askCum;
        for (const auto& lvl : bids)
            bidCum.push_back({lvl.price_, lvl.quantity_, (bidCum.empty() ? 0 : bidCum.back().cum) + lvl.quantity_});
        for (const auto& lvl : asks)
            askCum.push_back({lvl.price_, lvl.quantity_, (askCum.empty() ? 0 : askCum.back().cum) + lvl.quantity_});

        Quantity maxCum = 1;
        for (const auto& c : bidCum) maxCum = std::max(maxCum, c.cum);
        for (const auto& c : askCum) maxCum = std::max(maxCum, c.cum);

        auto nBidRows = std::min(bidCum.size(), kChartDepth);
        auto nAskRows = std::min(askCum.size(), kChartDepth);
        auto nRows = std::max(nBidRows, nAskRows);

        int center = tw / 2;

        {
            out << style::G7 << "\u2502" << style::R;
            std::string lbl = style::G + std::string("\u25c0 BIDS depth") + style::R;
            int leftLen = VisibleLen(lbl);
            out << "  " << lbl;
            out << std::string(static_cast<std::size_t>(std::max(0, center - leftLen - 3)), ' ');
            out << style::R_ << "ASKS depth \u25b6" << style::R;
            padLine("");
        }

        {
            out << style::G7 << "\u2502" << style::R << "  ";
            out << style::G7 << Repeat("\u2500", tw - 4) << style::R << "\n";
        }

        for (std::size_t r = 0; r < nRows; ++r) {
            out << style::G7 << "\u2502" << style::R << "  ";

            if (r < nBidRows) {
                const auto& c = bidCum[r];
                bool top = (r == 0);
                out << (top ? style::B : "") << (top ? style::BG : style::G);
                out << std::format("{:>7}", Comma(c.price)) << style::R;
                out << " " << (top ? style::B : "") << (top ? style::BG : style::G);
                out << std::format("{:>6}", Comma(c.qty)) << style::R;
                int bidEnd = VisibleLen(out.str());
                int bidBarMax = center - bidEnd - 1;
                int bidBarLen = (c.cum > 0) ? std::max(1, std::min(bidBarMax,
                    static_cast<int>(static_cast<std::uint64_t>(c.cum) * static_cast<std::uint64_t>(bidBarMax) / static_cast<std::uint64_t>(maxCum)))) : 0;
                out << (top ? style::B : "") << (top ? style::BG : style::G);
                out << Repeat("\u2588", bidBarLen) << style::R;
                int bidTotal = VisibleLen(out.str());
                if (bidTotal < center)
                    out << std::string(static_cast<std::size_t>(center - bidTotal), ' ');
            } else {
                int curPos = VisibleLen(out.str());
                if (curPos < center)
                    out << std::string(static_cast<std::size_t>(center - curPos), ' ');
            }

            out << style::G7 << "\u2502" << style::R;

            if (r < nAskRows) {
                const auto& c = askCum[r];
                bool top = (r == 0);
                int askBarMax = tw - center - 22;
                int askBarLen = (c.cum > 0) ? std::max(1, std::min(askBarMax,
                    static_cast<int>(static_cast<std::uint64_t>(c.cum) * static_cast<std::uint64_t>(askBarMax) / static_cast<std::uint64_t>(maxCum)))) : 0;
                out << " " << (top ? style::B : "") << (top ? style::BR : style::R_);
                out << Repeat("\u2588", askBarLen) << style::R;
                out << " " << (top ? style::B : "") << (top ? style::BR : style::R_);
                out << std::format("{:>6}", Comma(c.qty)) << style::R;
                out << " ";
                out << (top ? style::B : "") << (top ? style::BR : style::R_);
                out << std::format("{:>7}", Comma(c.price)) << style::R;
            }

            int rclen = VisibleLen(out.str());
            if (rclen < tw - 1)
                out << std::string(static_cast<std::size_t>(tw - 1 - rclen), ' ');
            out << style::G7 << "\u2502" << style::R << "\n";
        }

        {
            Quantity maxBidCum = bidCum.empty() ? 0 : bidCum.back().cum;
            Quantity maxAskCum = askCum.empty() ? 0 : askCum.back().cum;
            out << style::G7 << "\u2502" << style::R;
            out << "  " << style::D << "Max Bid Cum:" << style::R << " " << style::G << Comma(maxBidCum) << style::R;
            out << "  " << style::D << "Max Ask Cum:" << style::R << " " << style::R_ << Comma(maxAskCum) << style::R;
            padLine("");
        }
    } else {
        out << style::G7 << "\u2502" << style::R;
        out << "  " << style::G7 << "Waiting for orders..." << style::R;
        padLine("");
    }

    out << style::G7 << "\u251c" << style::R;
    out << style::G7 << Repeat("\u2500", tw - 2) << style::R;
    out << style::G7 << "\u2524" << style::R << "\n";

    {
        out << style::G7 << "\u2502" << style::R;
        out << "  " << style::B << style::BC << "RECENT ACTIVITY" << style::R;
        int alen = VisibleLen(out.str());
        if (alen < tw - 1) out << std::string(static_cast<std::size_t>(tw - 1 - alen), ' ');
        out << style::G7 << "\u2502" << style::R << "\n";
    }

    std::deque<EventEntry> logSnapshot;
    {
        std::lock_guard lock(logMutex_);
        logSnapshot = eventLog_;
    }

    for (const auto& entry : logSnapshot) {
        auto tt = std::chrono::system_clock::to_time_t(entry.time);
        std::tm tm{};
        localtime_r(&tt, &tm);
        auto ts = std::format("{:02}:{:02}:{:02}", tm.tm_hour, tm.tm_min, tm.tm_sec);

        const char* tc = style::C;
        if (entry.type == "ADDED") tc = style::G;
        else if (entry.type == "TRADE") tc = style::BC;
        else if (entry.type == "FILLED") tc = style::Y;
        else if (entry.type == "CANCEL") tc = style::M;
        else if (entry.type == "REJECT") tc = style::BR;
        else if (entry.type == "EXPIRED") tc = style::G7;
        else if (entry.type == "STOP") tc = style::B_;
        else if (entry.type == "TRIGGER") tc = style::BB;
        else if (entry.type == "PLACE") tc = style::G7;

        out << style::G7 << "\u2502" << style::R;
        out << "  " << style::G7 << ts << style::R;
        out << "  " << style::B << style::BY << entry.stockName << style::R;
        out << "  " << tc << "\u25c6 " << entry.type << style::R;
        out << "  " << entry.text;
        if (!entry.detail.empty())
            out << "  " << style::G7 << "(" << entry.detail << ")" << style::R;

        int elen = VisibleLen(out.str());
        if (elen < tw - 1) out << std::string(static_cast<std::size_t>(tw - 1 - elen), ' ');
        out << style::G7 << "\u2502" << style::R << "\n";
    }

    out << style::G7 << "\u251c" << style::R;
    out << style::G7 << Repeat("\u2500", tw - 2) << style::R;
    out << style::G7 << "\u2524" << style::R << "\n";

    std::size_t totalOrders = 0, totalTrades = 0;
    for (const auto& sctx : stocks_) { totalOrders += sctx.ordersPlaced; totalTrades += sctx.stats.GetTradeCount(); }

    {
        out << style::G7 << "\u2502" << style::R;
        out << "  " << style::G7 << "[" << style::R << style::B << style::BW << "1" << style::R << style::G7 << "]" << style::R << stocks_[0].name;
        for (std::size_t i = 1; i < kNumStocks; ++i) {
            out << "  " << style::G7 << "[" << style::R << style::B << style::BW << (i + 1) << style::R << style::G7 << "]" << style::R << stocks_[i].name;
        }
        out << "    ";
        out << style::G7 << "[" << style::R << style::B << style::BW << "P" << style::R << style::G7 << "]" << style::R;
        out << " Pause  ";
        out << style::G7 << "[" << style::R << style::B << style::BW << "Q" << style::R << style::G7 << "]" << style::R;
        out << " Quit";
        out << "    " << style::D << "Ord:" << style::R << " " << style::B << style::BW << totalOrders << style::R;
        out << style::D << "  Trd:" << style::R << " " << style::B << style::BW << totalTrades << style::R;

        int flen = VisibleLen(out.str());
        if (flen < tw - 1) out << std::string(static_cast<std::size_t>(tw - 1 - flen), ' ');
        out << style::G7 << "\u2502" << style::R << "\n";
    }

    out << style::G7 << "\u2514" << style::R;
    out << style::G7 << Repeat("\u2500", tw - 2) << style::R;
    out << style::G7 << "\u2518" << style::R << "\n";

    out << "\033[?25h";
    std::cout << out.str() << std::flush;
}

void Simulator::RestartFromWal() {
    if (!useWal_) return;

    paused_ = true;
    running_ = false;
    if (simThread_.joinable()) simThread_.join();

    MatchingEngine freshEngine;
    OrderId maxOrderId = 0;
    std::size_t replayed = Wal::Replay(walPath_, freshEngine, &maxOrderId);
    if (replayed > 0) {
        std::cout << "\nWAL: restarted from " << replayed << " operations"
                  << " (max OrderId=" << maxOrderId << ")\n";
    }

    {
        std::lock_guard lock(logMutex_);
        eventLog_.clear();
    }

    auto wal = std::make_unique<Wal>(walPath_);
    freshEngine.SetWal(wal.get());
    engine_ = std::move(freshEngine);

    for (auto& sc : stocks_) {
        sc.stats = TradeStats{};
        sc.ordersPlaced = 0;
        sc.fairValue = kStocks[&sc - stocks_.data()].baseFairValue;
    }

    SetupStockCallbacks(*this);

    nextOrderId_.store(maxOrderId + 1);
    startTime_ = std::chrono::steady_clock::now();
    rng_.seed(std::random_device{}());
    selectedStock_ = 0;

    running_ = true;
    paused_ = false;
    simThread_ = std::thread(&Simulator::SimulationLoop, this);
}

void Simulator::HandleKey(int key) {
    if (key == 'p' || key == 'P') paused_ = !paused_.load();
    else if (key == 'q' || key == 'Q') running_ = false;
    else if (key == '1') selectedStock_ = 0;
    else if (key == '2') selectedStock_ = (kNumStocks > 1) ? 1 : 0;
    else if (key == '3') selectedStock_ = (kNumStocks > 2) ? 2 : 0;
    else if (key == ' ') selectedStock_ = (selectedStock_ + 1) % kNumStocks;
    else if (key == 'r' || key == 'R') RestartFromWal();
}

void Simulator::Run() {
    Terminal term;
    simThread_ = std::thread(&Simulator::SimulationLoop, this);

    while (running_.load()) {
        Render(term);
        HandleKey(term.GetKeyPress(200));
    }

    running_ = false;
    if (simThread_.joinable()) simThread_.join();

    std::cout << "\033[2J\033[H";
    std::cout << style::B << style::BC << "\u25c6 Depth Chart ended." << style::R << "\n";
    std::size_t tot = 0;
    for (const auto& sc : stocks_) {
        std::cout << "  " << style::BY << sc.name << style::R
                  << ": " << sc.ordersPlaced << " orders, "
                  << sc.stats.GetTradeCount() << " trades, "
                  << sc.stats.GetTotalVolume() << " volume\n";
        tot += sc.ordersPlaced;
    }
    std::cout << style::D << "  Total: " << tot << " orders" << style::R << "\n";
}

void PrintUsage(const char* argv0) {
    std::cout << style::B << style::BC << "orderbook" << style::R
              << " — real-time order book matching engine simulator\n\n"
              << style::B << "Usage:" << style::R << "\n"
              << "  " << argv0 << " [--wal <path>] [--no-wal] [--help]\n\n"
              << style::B << "Options:" << style::R << "\n"
              << "  --wal <path>   WAL file path (default: orderbook.wal)\n"
              << "  --no-wal       Disable write-ahead logging\n"
              << "  --help         Show this help and exit\n\n"
              << style::D << "Interactive keys:" << style::R << "\n"
              << "  1-3 / Space   Switch stock\n"
              << "  P             Pause / resume simulation\n"
              << "  R             Restart from WAL file\n"
              << "  Q             Quit\n";
}

}

int main(int argc, char** argv) {
    std::string_view walPath = "orderbook.wal";
    bool useWal = true;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (arg == "--no-wal") {
            useWal = false;
        } else if (arg == "--wal" && i + 1 < argc) {
            walPath = argv[++i];
        }
    }

    MatchingEngine engine;
    OrderId maxOrderId = 0;

    if (useWal) {
        std::size_t replayed = Wal::Replay(std::string{walPath}, engine, &maxOrderId);
        if (replayed > 0) {
            std::cout << "WAL: recovered " << replayed << " operations"
                      << " (max OrderId=" << maxOrderId << ")\n";
        }
    }

    std::unique_ptr<Wal> wal;
    if (useWal) {
        wal = std::make_unique<Wal>(std::string{walPath});
        engine.SetWal(wal.get());
    }

    Simulator sim(std::move(engine), maxOrderId + 1,
                  std::string{walPath}, useWal);
    sim.Run();

    if (wal) {
        std::cout << "\nWAL: " << walPath << " (" << (wal->IsHealthy() ? "ok" : "err") << ")\n";
    }
    return EXIT_SUCCESS;
}
