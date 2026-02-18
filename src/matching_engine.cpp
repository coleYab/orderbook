#include "orderbook/matching_engine.h"

Stock::Stock(const StockId stockId, OrderBook orderBook)
    : stockId_{stockId}
    , orderBook_{std::move(orderBook)}
{}

StockTrade Stock::AddOrder(OrderPointer order) {
    const auto trades = orderBook_.AddOrder(order);
    return StockTrade{stockId_, std::move(trades)};
}

void Stock::CancelOrder(const OrderId orderId) {
    orderBook_.CancelOrder(orderId);
}

StockTrade Stock::ModifyOrder(const OrderModify& modify) {
    const auto trades = orderBook_.ModifyOrder(modify);
    return StockTrade{stockId_, std::move(trades)};
}

void Stock::SetEventCallback(const EventCallback callback) {
    orderBook_.SetEventCallback(callback);
}

OrderBookLevelInfos Stock::GetOrderLevelInfos() const {
    return orderBook_.GetOrderLevelInfos();
}

StockTrade MatchingEngine::PlaceOrder(const StockId stockId, OrderPointer order) {
    if (wal_) wal_->WriteAddOrder(stockId, *order);
    return GetOrCreateStock(stockId)->AddOrder(std::move(order));
}

void MatchingEngine::CancelOrder(const StockId stockId, const OrderId orderId) {
    if (wal_) wal_->WriteCancelOrder(stockId, orderId);
    GetOrCreateStock(stockId)->CancelOrder(orderId);
}

StockTrade MatchingEngine::ModifyOrder(const StockId stockId, const OrderModify& modify) {
    if (wal_) wal_->WriteModifyOrder(stockId, modify);
    return GetOrCreateStock(stockId)->ModifyOrder(modify);
}

void MatchingEngine::SetWal(Wal* const wal) {
    wal_ = wal;
}

void MatchingEngine::StealStocksFrom(MatchingEngine& other) {
    for (auto& [id, stock] : other.stocks_) {
        stocks_[id] = std::move(stock);
    }
    other.stocks_.clear();
    if (eventCallback_) {
        for (auto& [_, stock] : stocks_) {
            stock->SetEventCallback(eventCallback_);
        }
    }
}

void MatchingEngine::SetEventCallback(const EventCallback callback) {
    eventCallback_ = callback;
    for (const auto& [_, stock] : stocks_) {
        stock->SetEventCallback(eventCallback_);
    }
}

std::optional<OrderBookLevelInfos> MatchingEngine::GetOrderBookLevelInfos(const StockId stockId) const {
    const auto it = stocks_.find(stockId);
    if (it == stocks_.end()) return std::nullopt;
    return it->second->GetOrderLevelInfos();
}

StockPointer MatchingEngine::GetStock(const StockId stockId) const {
    const auto it = stocks_.find(stockId);
    return (it != stocks_.end()) ? it->second : StockPointer{};
}

StockPointer MatchingEngine::EnsureStock(const StockId stockId) {
    return GetOrCreateStock(stockId);
}

StockPointer MatchingEngine::GetOrCreateStock(const StockId stockId) {
    auto it = stocks_.find(stockId);
    if (it != stocks_.end()) return it->second;

    auto stock = std::make_shared<Stock>(stockId);
    if (eventCallback_) stock->SetEventCallback(eventCallback_);
    auto [newIt, _] = stocks_.emplace(stockId, std::move(stock));
    return newIt->second;
}
