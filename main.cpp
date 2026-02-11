#include <functional>
#include <iostream>
#include <map>
#include <utility>
#include <vector>
#include <string>
#include <memory>
#include <list>
#include <numeric>
#include <unordered_map>

enum class OrderType {
    GoodTillCancel,
    FillAndKill
};

enum class Side {
    Buy,
    Sell
};

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::int32_t;

struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderBookLevelInfos {
public:
    OrderBookLevelInfos(LevelInfos bids, LevelInfos asks) : bids_{std::move(bids)},
                                                            asks_{std::move(asks)} {
    }

    [[nodiscard]] const LevelInfos &GetBids() const {
        return bids_;
    }

    [[nodiscard]] const LevelInfos &GetAsks() const {
        return asks_;
    }

private:
    LevelInfos bids_;
    LevelInfos asks_;
};

class Order {
public:
    Order(const OrderType orderType, const OrderId orderId, const Side side, const Price price,
          const Quantity quantity)
        : orderType_{orderType}
          , orderId_{orderId}
          , side_{side}
          , price_{price}
          , initialQuantity_{quantity}
          , remainingQuantity_{quantity} {
    }

    [[nodiscard]] OrderId GetOrderId() const {
        return orderId_;
    }

    [[nodiscard]] OrderType GetOrderType() const {
        return orderType_;
    }

    [[nodiscard]] Side GetSide() const {
        return side_;
    }

    [[nodiscard]] Price GetPrice() const {
        return price_;
    }

    [[nodiscard]] Quantity GetInitialQuantity() const {
        return initialQuantity_;
    }

    [[nodiscard]] Quantity GetRemainingQuantity() const {
        return remainingQuantity_;
    }

    [[nodiscard]] Quantity GetFilledQuantity() const {
        return initialQuantity_ - remainingQuantity_;
    }

    [[nodiscard]] bool IsFilled() const {
        return remainingQuantity_ == 0;
    }

    void Fill(const Quantity quantity) {
        if (quantity > GetRemainingQuantity()) {
            throw std::logic_error{
                std::format("Order [{}] cannot be filled for more than its quantity.", GetOrderId())
            };
        }

        remainingQuantity_ -= quantity;
    }

private:
    OrderType orderType_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

class OrderModify {
public:
    OrderModify(const OrderId orderId, const Side side, const Price price, const Quantity quantity)
        : orderId_{orderId}
          , side_{side}
          , price_{price}
          , quantity_{quantity} {
    }

    [[nodiscard]] OrderId GetOrderId() const {
        return orderId_;
    }

    [[nodiscard]] Side GetSide() const {
        return side_;
    }

    [[nodiscard]] Price GetPrice() const {
        return price_;
    }

    [[nodiscard]] Quantity GetQuantity() const {
        return quantity_;
    }

    [[nodiscard]] OrderPointer ToOrderPointer(OrderType type) const {
        return std::make_shared<Order>(type, GetOrderId(), GetSide(), GetPrice(), GetQuantity());
    }

private:
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity quantity_;
};

struct TradeInfo {
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade {
public:
    Trade(const TradeInfo &bidTrade, const TradeInfo &askTrade)
        : bidTrade_{bidTrade}
          , askTrade_{askTrade} {
    }

    [[nodiscard]] const TradeInfo &GetBidTrade() const {
        return bidTrade_;
    }

    [[nodiscard]] const TradeInfo &GetAskTrade() const {
        return askTrade_;
    }

private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;

class OrderBook {
private:
    struct OrderEntry {
        OrderPointer order_{nullptr};
        OrderPointers::iterator location_;
    };

    std::map<Price, OrderPointers, std::greater<> > bids_;
    std::map<Price, OrderPointers, std::less<> > asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;

    bool CanMatch(const Side side, const Price price) const {
        if (side == Side::Buy) {
            if (asks_.empty()) return false;

            const auto &[bestAsk, _] = *asks_.begin();
            return price >= bestAsk;
        }

        if (bids_.empty()) return false;
        const auto &[bestBid, _] = *bids_.begin();
        return price <= bestBid;
    }

    Trades MatchOrder() {
        Trades trades;
        trades.reserve(orders_.size());
        while (true) {
            if (bids_.empty() || asks_.empty()) break;

            auto &[bidPrice, bids] = *bids_.begin();
            auto &[askPrice, asks] = *asks_.begin();

            if (bidPrice < askPrice) break;

            while (!bids.empty() && !asks.empty()) {
                const auto &bid = bids.front();
                const auto &ask = asks.front();

                const Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

                bid->Fill(quantity);
                ask->Fill(quantity);

                if (bid->IsFilled()) {
                    bids.pop_front();
                    orders_.erase(bid->GetOrderId());
                }

                if (ask->IsFilled()) {
                    asks.pop_front();
                    orders_.erase(ask->GetOrderId());
                }

                if (bids.empty()) {
                    bids_.erase(bidPrice);
                }

                if (asks.empty()) {
                    asks_.erase(askPrice);
                }

                trades.emplace_back(
                    TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity},
                    TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}
                );
            }
        }

        if (!bids_.empty()) {
            auto &[_, bids] = *bids_.begin();
            const auto &order = bids.front();
            if (order->GetOrderType() == OrderType::FillAndKill) {
                CancelOrder(order->GetOrderId());
            }
        }

        if (!asks_.empty()) {
            auto &[_, asks] = *asks_.begin();
            const auto &order = asks.front();
            if (order->GetOrderType() == OrderType::FillAndKill) {
                CancelOrder(order->GetOrderId());
            }
        }

        return trades;
    }

public:
    Trades AddOrder(const OrderPointer &order) {
        if (orders_.contains(order->GetOrderId())) return {};

        if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice())) {
            return {};
        }

        OrderPointers::iterator iterator;

        if (order->GetSide() == Side::Buy) {
            auto &orders = bids_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        } else {
            auto &orders = asks_[order->GetPrice()];
            orders.push_back(order);
            iterator = std::next(orders.begin(), orders.size() - 1);
        }

        orders_.insert({
            order->GetOrderId(), OrderEntry{
                order, iterator
            }
        });

        return MatchOrder();
    }

    void CancelOrder(const OrderId orderId) {
        if (!orders_.contains(orderId)) {
            return;
        }

        const auto &[order, iterator] = orders_.at(orderId);
        orders_.erase(orderId);

        if (order->GetSide() == Side::Sell) {
            const auto price = order->GetPrice();
            auto &orders = asks_.at(price);
            orders.erase(iterator);
            if (orders.empty()) {
                asks_.erase(price);
            }
        } else {
            const auto price = order->GetPrice();
            auto &orders = bids_.at(price);
            orders.erase(iterator);
            if (orders.empty()) {
                bids_.erase(price);
            }
        }
    }

    Trades MatchOrder(const OrderModify &order) {
        if (!orders_.contains(order.GetOrderId())) {
            return {};
        }

        const auto &[existingOrder, _] = orders_.at(order.GetOrderId());
        CancelOrder(order.GetOrderId());
        return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
    }

    [[nodiscard]] std::size_t Size() const {
        return orders_.size();
    }

    OrderBookLevelInfos GetOrderLevelInfos() const {
        LevelInfos bidInfos, askInfos;
        bidInfos.reserve(orders_.size());
        askInfos.reserve(orders_.size());

        auto CreateLevelInfos = [](const Price price, const OrderPointers &orders) {
            return LevelInfo{
                price,
                std::accumulate(orders.begin(), orders.end(), static_cast<Quantity>(0),
                                [](const std::size_t runningSum, const OrderPointer &order) {
                                    return runningSum + order->GetRemainingQuantity();
                                })
            };
        };

        for (const auto &[price, orders]: bids_) {
            bidInfos.push_back(CreateLevelInfos(price, orders));
        }

        for (const auto &[price, orders]: asks_) {
            askInfos.push_back(CreateLevelInfos(price, orders));
        }

        return OrderBookLevelInfos{bidInfos, askInfos};
    }
};


using StockId = std::int32_t;

struct StockTrade {
    StockId stockId_;
    Trades trades_;
};

class Stock {
public:
    Stock(const StockId stockId, OrderBook orderBook)
        : stockId_{stockId}
          , orderBook_{std::move(orderBook)} {
    }

    StockTrade AddOrder(const OrderPointer &order) {
        const auto trades = orderBook_.AddOrder(order);
        return StockTrade{stockId_, trades};
    }

    void CancelOrder(const OrderId orderId) {
        orderBook_.CancelOrder(orderId);
    }

private:
    StockId stockId_;
    OrderBook orderBook_;
};

using StockPointer = std::shared_ptr<Stock>;

class MatchingEngine {
public:
    MatchingEngine() = default;

    StockTrade PlaceOrder(const StockId stockId, const OrderPointer &order) {
        const auto &stock = GetStock(stockId);
        return stock->AddOrder(order);
    }

    void CancelOrder(const StockId stockId, const OrderId orderId) {
        const auto &stock = GetStock(stockId);
        stock->CancelOrder(orderId);
    }

private:
    StockPointer GetStock(const StockId stockId) {
        if (!stocks_.contains(stockId)) {
            stocks_[stockId] = std::make_shared<Stock>(stockId, OrderBook{});
        }

        return stocks_[stockId];
    }

    std::unordered_map<StockId, StockPointer> stocks_;
};

int main() {
    return 0;
}
