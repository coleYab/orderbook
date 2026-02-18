#include "orderbook/market_data.h"

#include <algorithm>
#include <limits>
#include <numeric>

void MarketDataCollector::OnEvent(const OrderBookEvent& event) {
    auto* trade = std::get_if<TradeEvent>(&event);
    if (!trade) return;

    TradeRecord r{
        nextTradeId_++,
        trade->bidOrder_->GetOrderId(),
        trade->askOrder_->GetOrderId(),
        trade->price_,
        trade->quantity_,
        std::chrono::system_clock::now()
    };

    if (trades_.size() >= kMaxTrades) {
        trades_.erase(trades_.begin());
    }
    trades_.push_back(r);

    totalVolume_ += trade->quantity_;
    volumePriceSum_ += static_cast<double>(trade->price_) * static_cast<double>(trade->quantity_);
    ++tradeCount_;
}

const std::vector<TradeRecord>& MarketDataCollector::GetTradeHistory() const {
    return trades_;
}

Quantity MarketDataCollector::GetTotalVolume() const {
    return totalVolume_;
}

double MarketDataCollector::GetVWAP() const {
    if (totalVolume_ == 0) return 0.0;
    return volumePriceSum_ / static_cast<double>(totalVolume_);
}

std::uint64_t MarketDataCollector::GetTradeCount() const {
    return tradeCount_;
}

MarketStats MarketDataCollector::ComputeStats(const OrderBook& book) const {
    MarketStats stats;

    stats.totalVolume_ = totalVolume_;
    stats.vwap_ = GetVWAP();
    stats.tradeCount_ = tradeCount_;

    const auto topBid = book.GetTopOfBid();
    const auto topAsk = book.GetTopOfAsk();

    if (topBid.has_value()) {
        stats.bestBid_ = topBid->price_;
        stats.bidSize_ = topBid->quantity_;
    }
    if (topAsk.has_value()) {
        stats.bestAsk_ = topAsk->price_;
        stats.askSize_ = topAsk->quantity_;
    }
    if (topBid.has_value() && topAsk.has_value()) {
        stats.spread_ = topAsk->price_ - topBid->price_;
        stats.midPrice_ = (topBid->price_ + topAsk->price_) / 2;
    }

    return stats;
}

BookDepth::BookDepth(const std::size_t depth)
    : depth_{depth}
{}

void BookDepth::ExtractFrom(const OrderBook& book) {
    const auto levelInfos = book.GetOrderLevelInfos();
    const auto& allBids = levelInfos.GetBids();
    const auto& allAsks = levelInfos.GetAsks();

    bids_.clear();
    asks_.clear();

    const auto take = [depth = depth_](const auto& src, auto& dst) {
        const auto n = std::min(depth, src.size());
        dst.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            dst.push_back(src[i]);
        }
    };

    take(allBids, bids_);
    take(allAsks, asks_);
}

const LevelInfos& BookDepth::GetBids() const {
    return bids_;
}

const LevelInfos& BookDepth::GetAsks() const {
    return asks_;
}

std::size_t BookDepth::GetDepth() const {
    return depth_;
}
