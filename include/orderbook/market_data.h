#pragma once

#include "event.h"
#include "level_info.h"
#include "order_book.h"

#include <cstdint>
#include <optional>
#include <vector>

struct TradeRecord {
    std::uint64_t tradeId_;
    OrderId bidOrderId_;
    OrderId askOrderId_;
    Price price_;
    Quantity quantity_;
    TimePoint timestamp_;
};

struct MarketStats {
    std::optional<Price> bestBid_;
    std::optional<Price> bestAsk_;
    Price spread_{};
    Price midPrice_{};
    Quantity bidSize_{};
    Quantity askSize_{};
    Quantity totalVolume_{};
    double vwap_{};
    std::uint64_t tradeCount_{};
};

class MarketDataCollector {
public:
    MarketDataCollector() = default;

    void OnEvent(const OrderBookEvent& event);

    [[nodiscard]] const std::vector<TradeRecord>& GetTradeHistory() const;
    [[nodiscard]] Quantity GetTotalVolume() const;
    [[nodiscard]] double GetVWAP() const;
    [[nodiscard]] std::uint64_t GetTradeCount() const;

    MarketStats ComputeStats(const OrderBook& book) const;

private:
    static constexpr std::size_t kMaxTrades = 5000;

    std::vector<TradeRecord> trades_;
    std::uint64_t nextTradeId_ = 1;
    Quantity totalVolume_{};
    double volumePriceSum_{};
    std::uint64_t tradeCount_{};
};

class BookDepth {
public:
    explicit BookDepth(std::size_t depth = 10);

    void ExtractFrom(const OrderBook& book);

    [[nodiscard]] const LevelInfos& GetBids() const;
    [[nodiscard]] const LevelInfos& GetAsks() const;
    [[nodiscard]] std::size_t GetDepth() const;

private:
    std::size_t depth_;
    LevelInfos bids_;
    LevelInfos asks_;
};
