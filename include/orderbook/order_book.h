#pragma once

#include "event.h"
#include "level_info.h"
#include "order.h"
#include "trade.h"

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

class OrderBook {
public:
    OrderBook() = default;
    OrderBook(OrderBook&& other) noexcept;
    OrderBook& operator=(OrderBook&& other) noexcept;

    Trades AddOrder(OrderPointer order);
    void CancelOrder(OrderId orderId);
    Trades ModifyOrder(const OrderModify& modify);

    void SetEventCallback(EventCallback callback);

    [[nodiscard]] std::size_t Size() const;
    [[nodiscard]] OrderBookLevelInfos GetOrderLevelInfos() const;

    [[nodiscard]] std::optional<LevelInfo> GetTopOfBid() const;
    [[nodiscard]] std::optional<LevelInfo> GetTopOfAsk() const;

private:
    struct OrderEntry {
        OrderPointer order_{nullptr};
        OrderPointers::iterator location_;
    };

    mutable std::shared_mutex mutex_;
    EventCallback eventCallback_;
    mutable std::vector<OrderBookEvent> eventQueue_;

    void Emit(const OrderBookEvent& event) const;
    void FlushEvents() const;

    // Internal impl (no locking — called under lock by public wrappers)
    Trades AddOrderImpl(OrderPointer order);
    void CancelOrderImpl(OrderId orderId);
    Trades ModifyOrderImpl(const OrderModify& modify);

    std::map<Price, OrderPointers, std::greater<>> bids_;
    std::map<Price, OrderPointers, std::less<>> asks_;
    std::unordered_map<OrderId, OrderEntry> orders_;

    std::unordered_map<OrderId, OrderPointer> stopOrders_;
    std::multiset<std::pair<Price, OrderId>> buyStops_;
    std::multiset<std::pair<Price, OrderId>> sellStops_;

    int stopTriggerDepth_ = 0;
    static constexpr int kMaxStopDepth = 10;

    [[nodiscard]] bool CanMatch(Side side, Price price) const;
    [[nodiscard]] bool CanFullyFill(const Order& order) const;
    Trades MatchOrder();
    Trades ExecuteMarketOrder(const OrderPointer& order);
    void RemoveOrder(OrderId orderId);
    void CancelStopOrder(OrderId orderId);
    Trades TriggerStopOrders(const std::unordered_set<Price>& tradePrices);
    void SweepExpiredOrders();
};
