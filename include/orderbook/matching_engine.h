#pragma once

#include "order_book.h"
#include "wal.h"

#include <memory>
#include <unordered_map>

struct StockTrade {
    StockId stockId_;
    Trades trades_;
};

class Stock {
public:
    explicit Stock(StockId stockId, OrderBook orderBook = OrderBook{});

    StockTrade AddOrder(OrderPointer order);
    void CancelOrder(OrderId orderId);
    StockTrade ModifyOrder(const OrderModify& modify);

    void SetEventCallback(EventCallback callback);

    [[nodiscard]] OrderBookLevelInfos GetOrderLevelInfos() const;

private:
    StockId stockId_;
    OrderBook orderBook_;
};

using StockPointer = std::shared_ptr<Stock>;

class MatchingEngine {
public:
    MatchingEngine() = default;

    StockTrade PlaceOrder(StockId stockId, OrderPointer order);
    void CancelOrder(StockId stockId, OrderId orderId);
    StockTrade ModifyOrder(StockId stockId, const OrderModify& modify);

    void SetEventCallback(EventCallback callback);
    void SetWal(Wal* wal);

    void StealStocksFrom(MatchingEngine& other);

    [[nodiscard]] std::optional<OrderBookLevelInfos> GetOrderBookLevelInfos(StockId stockId) const;
    [[nodiscard]] StockPointer GetStock(StockId stockId) const;
    StockPointer EnsureStock(StockId stockId);

private:
    StockPointer GetOrCreateStock(StockId stockId);
    EventCallback eventCallback_;
    Wal* wal_{nullptr};
    std::unordered_map<StockId, StockPointer> stocks_;
};
