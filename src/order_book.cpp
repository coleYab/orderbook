#include "orderbook/order_book.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <numeric>

OrderBook::OrderBook(OrderBook&& other) noexcept
    : eventCallback_{std::move(other.eventCallback_)}
    , bids_{std::move(other.bids_)}
    , asks_{std::move(other.asks_)}
    , orders_{std::move(other.orders_)}
    , stopOrders_{std::move(other.stopOrders_)}
    , buyStops_{std::move(other.buyStops_)}
    , sellStops_{std::move(other.sellStops_)}
    , stopTriggerDepth_{other.stopTriggerDepth_}
{}

OrderBook& OrderBook::operator=(OrderBook&& other) noexcept {
    if (this != &other) {
        std::unique_lock lock(mutex_);
        eventCallback_ = std::move(other.eventCallback_);
        bids_ = std::move(other.bids_);
        asks_ = std::move(other.asks_);
        orders_ = std::move(other.orders_);
        stopOrders_ = std::move(other.stopOrders_);
        buyStops_ = std::move(other.buyStops_);
        sellStops_ = std::move(other.sellStops_);
        stopTriggerDepth_ = other.stopTriggerDepth_;
    }
    return *this;
}

void OrderBook::SetEventCallback(EventCallback callback) {
    std::unique_lock lock(mutex_);
    eventCallback_ = std::move(callback);
}

Trades OrderBook::AddOrder(OrderPointer order) {
    std::unique_lock lock(mutex_);
    auto trades = AddOrderImpl(std::move(order));
    FlushEvents();
    return trades;
}

void OrderBook::CancelOrder(OrderId orderId) {
    std::unique_lock lock(mutex_);
    CancelOrderImpl(orderId);
    FlushEvents();
}

Trades OrderBook::ModifyOrder(const OrderModify& modify) {
    std::unique_lock lock(mutex_);
    auto trades = ModifyOrderImpl(modify);
    FlushEvents();
    return trades;
}

std::size_t OrderBook::Size() const {
    std::shared_lock lock(mutex_);
    return orders_.size() + stopOrders_.size();
}

OrderBookLevelInfos OrderBook::GetOrderLevelInfos() const {
    std::shared_lock lock(mutex_);
    LevelInfos bidInfos;
    LevelInfos askInfos;
    bidInfos.reserve(bids_.size());
    askInfos.reserve(asks_.size());

    const auto makeLevelInfo = [](const Price price, const OrderPointers& orders) {
        Quantity total = 0;
        for (const auto& o : orders) {
            total += o->GetRemainingQuantity();
        }
        return LevelInfo{price, total};
    };

    for (const auto& [price, orders] : bids_) {
        bidInfos.push_back(makeLevelInfo(price, orders));
    }
    for (const auto& [price, orders] : asks_) {
        askInfos.push_back(makeLevelInfo(price, orders));
    }

    return OrderBookLevelInfos{std::move(bidInfos), std::move(askInfos)};
}

std::optional<LevelInfo> OrderBook::GetTopOfBid() const {
    std::shared_lock lock(mutex_);
    if (bids_.empty()) return std::nullopt;
    const auto& [price, orders] = *bids_.begin();
    Quantity total = 0;
    for (const auto& o : orders) total += o->GetRemainingQuantity();
    return LevelInfo{price, total};
}

std::optional<LevelInfo> OrderBook::GetTopOfAsk() const {
    std::shared_lock lock(mutex_);
    if (asks_.empty()) return std::nullopt;
    const auto& [price, orders] = *asks_.begin();
    Quantity total = 0;
    for (const auto& o : orders) total += o->GetRemainingQuantity();
    return LevelInfo{price, total};
}

void OrderBook::Emit(const OrderBookEvent& event) const {
    eventQueue_.push_back(event);
}

void OrderBook::FlushEvents() const {
    if (!eventCallback_) {
        eventQueue_.clear();
        return;
    }
    for (const auto& e : eventQueue_) {
        eventCallback_(e);
    }
    eventQueue_.clear();
}

bool OrderBook::CanMatch(Side side, Price price) const {
    if (side == Side::Buy) {
        if (asks_.empty()) return false;
        return price >= asks_.begin()->first;
    }
    if (bids_.empty()) return false;
    return price <= bids_.begin()->first;
}

bool OrderBook::CanFullyFill(const Order& order) const {
    Quantity available = 0;

    if (order.GetSide() == Side::Buy) {
        for (const auto& [price, orders] : asks_) {
            if (price > order.GetPrice()) break;
            for (const auto& o : orders) {
                available += o->GetRemainingQuantity();
                if (available >= order.GetRemainingQuantity()) return true;
            }
        }
    } else {
        for (const auto& [price, orders] : bids_) {
            if (price < order.GetPrice()) break;
            for (const auto& o : orders) {
                available += o->GetRemainingQuantity();
                if (available >= order.GetRemainingQuantity()) return true;
            }
        }
    }

    return false;
}

Trades OrderBook::MatchOrder() {
    Trades trades;
    trades.reserve(orders_.size());

    while (true) {
        if (bids_.empty() || asks_.empty()) break;

        const Price bidPrice = bids_.begin()->first;
        const Price askPrice = asks_.begin()->first;

        if (bidPrice < askPrice) break;

        auto& bids = bids_.begin()->second;
        auto& asks = asks_.begin()->second;

        while (!bids.empty() && !asks.empty()) {
            auto& bid = bids.front();
            auto& ask = asks.front();

            const Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());

            bid->Fill(quantity);
            ask->Fill(quantity);

            trades.emplace_back(
                TradeInfo{bid->GetOrderId(), bid->GetPrice(), quantity},
                TradeInfo{ask->GetOrderId(), ask->GetPrice(), quantity}
            );

            Emit(TradeEvent{bid, ask, ask->GetPrice(), quantity});

            const bool bidConsumed = bid->IsFilled();
            const bool askConsumed = ask->IsFilled();

            if (bidConsumed) {
                Emit(OrderFilledEvent{bid});
                if (bid->IsIceberg() && bid->HasHiddenRemaining()) {
                    bid->ReplenishPeak();
                } else {
                    orders_.erase(bid->GetOrderId());
                    bids.pop_front();
                }
            }

            if (askConsumed) {
                Emit(OrderFilledEvent{ask});
                if (ask->IsIceberg() && ask->HasHiddenRemaining()) {
                    ask->ReplenishPeak();
                } else {
                    orders_.erase(ask->GetOrderId());
                    asks.pop_front();
                }
            }
        }

        if (!bids_.empty() && bids_.begin()->second.empty()) bids_.erase(bids_.begin());
        if (!asks_.empty() && asks_.begin()->second.empty()) asks_.erase(asks_.begin());
    }

    if (!bids_.empty()) {
        const auto& order = bids_.begin()->second.front();
        if (order->GetTimeInForce() == TimeInForce::ImmediateOrCancel) {
            Emit(OrderCancelledEvent{order});
            CancelOrderImpl(order->GetOrderId());
        }
    }

    if (!asks_.empty()) {
        const auto& order = asks_.begin()->second.front();
        if (order->GetTimeInForce() == TimeInForce::ImmediateOrCancel) {
            Emit(OrderCancelledEvent{order});
            CancelOrderImpl(order->GetOrderId());
        }
    }

    return trades;
}

Trades OrderBook::ExecuteMarketOrder(const OrderPointer& order) {
    Trades trades;
    Quantity remaining = order->GetRemainingQuantity();

    if (order->GetSide() == Side::Buy) {
        while (remaining > 0 && !asks_.empty()) {
            const Price price = asks_.begin()->first;
            auto& asks = asks_.begin()->second;

            while (remaining > 0 && !asks.empty()) {
                auto& ask = asks.front();
                const Quantity matchQty = std::min(remaining, ask->GetRemainingQuantity());
                ask->Fill(matchQty);
                remaining -= matchQty;

                trades.emplace_back(
                    TradeInfo{order->GetOrderId(), price, matchQty},
                    TradeInfo{ask->GetOrderId(), price, matchQty}
                );

                Emit(TradeEvent{order, ask, price, matchQty});

                if (ask->IsFilled()) {
                    Emit(OrderFilledEvent{ask});
                    if (ask->IsIceberg() && ask->HasHiddenRemaining()) {
                        ask->ReplenishPeak();
                    } else {
                        orders_.erase(ask->GetOrderId());
                        asks.pop_front();
                    }
                }
            }

            if (!asks_.empty() && asks_.begin()->second.empty()) asks_.erase(asks_.begin());
        }
    } else {
        while (remaining > 0 && !bids_.empty()) {
            const Price price = bids_.begin()->first;
            auto& bids = bids_.begin()->second;

            while (remaining > 0 && !bids.empty()) {
                auto& bid = bids.front();
                const Quantity matchQty = std::min(remaining, bid->GetRemainingQuantity());
                bid->Fill(matchQty);
                remaining -= matchQty;

                trades.emplace_back(
                    TradeInfo{bid->GetOrderId(), price, matchQty},
                    TradeInfo{order->GetOrderId(), price, matchQty}
                );

                Emit(TradeEvent{bid, order, price, matchQty});

                if (bid->IsFilled()) {
                    Emit(OrderFilledEvent{bid});
                    if (bid->IsIceberg() && bid->HasHiddenRemaining()) {
                        bid->ReplenishPeak();
                    } else {
                        orders_.erase(bid->GetOrderId());
                        bids.pop_front();
                    }
                }
            }

            if (!bids_.empty() && bids_.begin()->second.empty()) bids_.erase(bids_.begin());
        }
    }

    return trades;
}

Trades OrderBook::TriggerStopOrders(const std::unordered_set<Price>& tradePrices) {
    if (++stopTriggerDepth_ > kMaxStopDepth) {
        --stopTriggerDepth_;
        return {};
    }

    std::vector<OrderPointer> triggered;

    for (const Price tradePrice : tradePrices) {
        auto it = buyStops_.begin();
        while (it != buyStops_.end() && it->first <= tradePrice) {
            const auto& [_, orderId] = *it;
            const auto stopOrder = stopOrders_[orderId];
            const auto triggerOrder = CreateTriggerOrder(*stopOrder);
            Emit(StopTriggeredEvent{stopOrder, triggerOrder});
            triggered.push_back(triggerOrder);
            stopOrders_.erase(orderId);
            it = buyStops_.erase(it);
        }

        auto sit = sellStops_.lower_bound({tradePrice, 0});
        while (sit != sellStops_.end()) {
            const auto& [_, orderId] = *sit;
            const auto stopOrder = stopOrders_[orderId];
            const auto triggerOrder = CreateTriggerOrder(*stopOrder);
            Emit(StopTriggeredEvent{stopOrder, triggerOrder});
            triggered.push_back(triggerOrder);
            stopOrders_.erase(orderId);
            sit = sellStops_.erase(sit);
        }
    }

    Trades allTrades;
    for (auto& triggeredOrder : triggered) {
        auto trades = AddOrderImpl(std::move(triggeredOrder));
        allTrades.insert(allTrades.end(), trades.begin(), trades.end());
    }

    --stopTriggerDepth_;
    return allTrades;
}

void OrderBook::SweepExpiredOrders() {
    std::vector<OrderPointer> expired;
    for (const auto& [orderId, entry] : orders_) {
        if (entry.order_->IsExpired()) {
            expired.push_back(entry.order_);
        }
    }
    for (const auto& order : expired) {
        Emit(OrderExpiredEvent{order});
        RemoveOrder(order->GetOrderId());
    }
}

void OrderBook::RemoveOrder(OrderId orderId) {
    const auto it = orders_.find(orderId);
    if (it == orders_.end()) return;

    const auto& [order, iterator] = it->second;
    const Price price = order->GetPrice();

    if (order->GetSide() == Side::Sell) {
        auto& orders = asks_.at(price);
        orders.erase(iterator);
        if (orders.empty()) asks_.erase(price);
    } else {
        auto& orders = bids_.at(price);
        orders.erase(iterator);
        if (orders.empty()) bids_.erase(price);
    }

    orders_.erase(it);
}

Trades OrderBook::AddOrderImpl(OrderPointer order) {
    SweepExpiredOrders();

    if (order->IsStop()) {
        if (stopOrders_.contains(order->GetOrderId())) return {};
        const OrderId orderId = order->GetOrderId();
        const Price stopPrice = order->GetStopPrice();

        stopOrders_.emplace(orderId, order);

        if (order->GetSide() == Side::Buy) {
            buyStops_.insert({stopPrice, orderId});
        } else {
            sellStops_.insert({stopPrice, orderId});
        }

        Emit(StopPlacedEvent{order});
        return {};
    }

    if (orders_.contains(order->GetOrderId())) {
        Emit(OrderRejectedEvent{order, "duplicate order id"});
        return {};
    }

    if (order->IsExpired()) {
        Emit(OrderRejectedEvent{order, "order expired"});
        return {};
    }

    if (order->GetTimeInForce() == TimeInForce::FillOrKill) {
        if (!CanFullyFill(*order)) {
            Emit(OrderRejectedEvent{order, "cannot fully fill"});
            return {};
        }
    }

    if (order->IsMarket()) {
        if ((order->GetSide() == Side::Buy && asks_.empty())
            || (order->GetSide() == Side::Sell && bids_.empty())) {
            Emit(OrderRejectedEvent{order, "no liquidity"});
            return {};
        }

        auto trades = ExecuteMarketOrder(order);

        if (!trades.empty() && stopTriggerDepth_ < kMaxStopDepth) {
            std::unordered_set<Price> tradePrices;
            for (const auto& t : trades) tradePrices.insert(t.GetBidTrade().price_);
            auto triggered = TriggerStopOrders(tradePrices);
            trades.insert(trades.end(), triggered.begin(), triggered.end());
        }

        return trades;
    }

    if (order->GetTimeInForce() == TimeInForce::ImmediateOrCancel) {
        if (!CanMatch(order->GetSide(), order->GetPrice())) {
            Emit(OrderRejectedEvent{order, "cannot match"});
            return {};
        }
    }

    const OrderId orderId = order->GetOrderId();
    const OrderPointer orderPtr = order;
    OrderPointers::iterator iterator;

    if (order->GetSide() == Side::Buy) {
        auto& orders = bids_[order->GetPrice()];
        orders.push_back(std::move(order));
        iterator = std::prev(orders.end());
    } else {
        auto& orders = asks_[order->GetPrice()];
        orders.push_back(std::move(order));
        iterator = std::prev(orders.end());
    }

    orders_.insert({orderId, OrderEntry{orderPtr, iterator}});
    Emit(OrderAddedEvent{orderPtr});

    auto trades = MatchOrder();

    if (!trades.empty() && stopTriggerDepth_ < kMaxStopDepth) {
        std::unordered_set<Price> tradePrices;
        for (const auto& t : trades) tradePrices.insert(t.GetBidTrade().price_);
        auto triggered = TriggerStopOrders(tradePrices);
        trades.insert(trades.end(), triggered.begin(), triggered.end());
    }

    return trades;
}

void OrderBook::CancelOrderImpl(OrderId orderId) {
    const auto it = orders_.find(orderId);
    if (it != orders_.end()) {
        Emit(OrderCancelledEvent{it->second.order_});
        RemoveOrder(orderId);
        return;
    }

    CancelStopOrder(orderId);
}

void OrderBook::CancelStopOrder(OrderId orderId) {
    const auto it = stopOrders_.find(orderId);
    if (it == stopOrders_.end()) return;

    const auto& order = it->second;

    Emit(OrderCancelledEvent{order});

    if (order->GetSide() == Side::Buy) {
        const auto range = buyStops_.equal_range({order->GetStopPrice(), orderId});
        for (auto sit = range.first; sit != range.second; ++sit) {
            if (sit->second == orderId) {
                buyStops_.erase(sit);
                break;
            }
        }
    } else {
        const auto range = sellStops_.equal_range({order->GetStopPrice(), orderId});
        for (auto sit = range.first; sit != range.second; ++sit) {
            if (sit->second == orderId) {
                sellStops_.erase(sit);
                break;
            }
        }
    }

    stopOrders_.erase(it);
}

Trades OrderBook::ModifyOrderImpl(const OrderModify& modify) {
    const auto it = orders_.find(modify.GetOrderId());
    if (it == orders_.end()) return {};

    const OrderPointer existingOrder = it->second.order_;
    CancelOrderImpl(modify.GetOrderId());
    return AddOrderImpl(modify.ToOrderPointer(*existingOrder));
}
