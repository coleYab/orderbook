#include <gtest/gtest.h>

#include "orderbook/market_data.h"
#include "orderbook/matching_engine.h"

#include <vector>

using namespace std::chrono_literals;

namespace {

auto LimitGtc(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillCancel,
                                   id, side, price, qty);
}

auto MarketOrder(OrderId id, Side side, Quantity qty) {
    return std::make_shared<Order>(OrderType::Market, TimeInForce::ImmediateOrCancel,
                                   id, side, Price{0}, qty);
}

}

TEST(MarketDataTest, NoTradesInitially) {
    MarketDataCollector collector;
    EXPECT_TRUE(collector.GetTradeHistory().empty());
    EXPECT_EQ(collector.GetTotalVolume(), 0u);
    EXPECT_EQ(collector.GetVWAP(), 0.0);
    EXPECT_EQ(collector.GetTradeCount(), 0u);
}

TEST(MarketDataTest, CollectsTradeFromEvent) {
    MarketDataCollector collector;

    const auto bid = LimitGtc(1, Side::Buy, 100, 10);
    const auto ask = LimitGtc(2, Side::Sell, 100, 5);

    bid->Fill(5);
    ask->Fill(5);

    collector.OnEvent(TradeEvent{bid, ask, 100, 5});

    ASSERT_EQ(collector.GetTradeHistory().size(), 1u);
    EXPECT_EQ(collector.GetTradeHistory()[0].bidOrderId_, 1);
    EXPECT_EQ(collector.GetTradeHistory()[0].askOrderId_, 2);
    EXPECT_EQ(collector.GetTradeHistory()[0].price_, 100);
    EXPECT_EQ(collector.GetTradeHistory()[0].quantity_, 5);
}

TEST(MarketDataTest, TracksTotalVolumeAndVwap) {
    MarketDataCollector collector;

    auto bid1 = LimitGtc(1, Side::Buy, 100, 10);
    auto ask1 = LimitGtc(2, Side::Sell, 100, 5);
    auto bid2 = LimitGtc(3, Side::Buy, 101, 10);
    auto ask2 = LimitGtc(4, Side::Sell, 101, 3);

    bid1->Fill(5); ask1->Fill(5);
    bid2->Fill(3); ask2->Fill(3);

    collector.OnEvent(TradeEvent{bid1, ask1, 100, 5});
    collector.OnEvent(TradeEvent{bid2, ask2, 101, 3});

    EXPECT_EQ(collector.GetTotalVolume(), 8u);
    EXPECT_EQ(collector.GetTradeCount(), 2u);

    EXPECT_DOUBLE_EQ(collector.GetVWAP(), 100.375);
}

TEST(MarketDataTest, RespectsMaxTrades) {
    MarketDataCollector collector;

    constexpr std::size_t numTrades = 6000;
    for (std::size_t i = 0; i < numTrades; ++i) {
        auto bid = LimitGtc(static_cast<OrderId>(i * 2), Side::Buy, 100, 10);
        auto ask = LimitGtc(static_cast<OrderId>(i * 2 + 1), Side::Sell, 100, 10);
        bid->Fill(1); ask->Fill(1);
        collector.OnEvent(TradeEvent{bid, ask, 100, 1});
    }

    EXPECT_LE(collector.GetTradeHistory().size(), 5000u);
    EXPECT_EQ(collector.GetTotalVolume(), static_cast<Quantity>(numTrades));
}

TEST(MarketDataTest, StatsWithEmptyBook) {
    OrderBook book;
    MarketDataCollector collector;

    const auto stats = collector.ComputeStats(book);

    EXPECT_FALSE(stats.bestBid_.has_value());
    EXPECT_FALSE(stats.bestAsk_.has_value());
    EXPECT_EQ(stats.totalVolume_, 0u);
    EXPECT_EQ(stats.tradeCount_, 0u);
}

TEST(MarketDataTest, StatsWithActiveBookAndTrades) {
    OrderBook book;
    MarketDataCollector collector;

    book.AddOrder(LimitGtc(1, Side::Buy, 100, 10));
    book.AddOrder(LimitGtc(2, Side::Sell, 101, 5));

    auto bid = LimitGtc(3, Side::Buy, 100, 5);
    auto ask = LimitGtc(4, Side::Sell, 100, 5);
    bid->Fill(5); ask->Fill(5);
    collector.OnEvent(TradeEvent{bid, ask, 100, 5});

    const auto stats = collector.ComputeStats(book);

    ASSERT_TRUE(stats.bestBid_.has_value());
    ASSERT_TRUE(stats.bestAsk_.has_value());
    EXPECT_EQ(*stats.bestBid_, 100);
    EXPECT_EQ(*stats.bestAsk_, 101);
    EXPECT_EQ(stats.spread_, 1);
    EXPECT_EQ(stats.midPrice_, 100);
    EXPECT_EQ(stats.totalVolume_, 5u);
    EXPECT_EQ(stats.tradeCount_, 1u);
    EXPECT_DOUBLE_EQ(stats.vwap_, 100.0);
}

TEST(MarketDataTest, IntegrationWithOrderBook) {
    OrderBook book;
    MarketDataCollector collector;

    book.SetEventCallback([&](const OrderBookEvent& event) {
        collector.OnEvent(event);
    });

    book.AddOrder(LimitGtc(1, Side::Sell, 100, 10));

    book.AddOrder(LimitGtc(2, Side::Buy, 100, 4));

    ASSERT_EQ(collector.GetTradeCount(), 1u);
    EXPECT_EQ(collector.GetTotalVolume(), 4u);
    EXPECT_DOUBLE_EQ(collector.GetVWAP(), 100.0);

    book.AddOrder(LimitGtc(3, Side::Buy, 100, 3));

    ASSERT_EQ(collector.GetTradeCount(), 2u);
    EXPECT_EQ(collector.GetTotalVolume(), 7u);

    const auto stats = collector.ComputeStats(book);
    EXPECT_FALSE(stats.bestBid_.has_value());
    ASSERT_TRUE(stats.bestAsk_.has_value());
    EXPECT_EQ(*stats.bestAsk_, 100);
    EXPECT_EQ(stats.askSize_, 3u);
    EXPECT_EQ(stats.totalVolume_, 7u);
    EXPECT_EQ(stats.tradeCount_, 2u);
}

TEST(MarketDataTest, IntegrationWithMatchingEngine) {
    MatchingEngine engine;
    MarketDataCollector collector;

    engine.SetEventCallback([&](const OrderBookEvent& event) {
        collector.OnEvent(event);
    });

    engine.PlaceOrder(1, LimitGtc(1, Side::Sell, 100, 10));
    engine.PlaceOrder(1, LimitGtc(3, Side::Buy, 100, 6));

    ASSERT_EQ(collector.GetTradeCount(), 1u);
    EXPECT_EQ(collector.GetTotalVolume(), 6u);
}

TEST(MarketDataTest, BookDepthEmpty) {
    OrderBook book;
    BookDepth depth(5);

    depth.ExtractFrom(book);
    EXPECT_TRUE(depth.GetBids().empty());
    EXPECT_TRUE(depth.GetAsks().empty());
    EXPECT_EQ(depth.GetDepth(), 5u);
}

TEST(MarketDataTest, BookDepthRespectsLimit) {
    OrderBook book;

    book.AddOrder(LimitGtc(1, Side::Buy, 100, 10));
    book.AddOrder(LimitGtc(2, Side::Buy, 99, 20));
    book.AddOrder(LimitGtc(3, Side::Buy, 98, 30));
    book.AddOrder(LimitGtc(4, Side::Sell, 101, 15));
    book.AddOrder(LimitGtc(5, Side::Sell, 102, 25));
    book.AddOrder(LimitGtc(6, Side::Sell, 103, 35));

    BookDepth depth2(2);
    depth2.ExtractFrom(book);
    ASSERT_EQ(depth2.GetBids().size(), 2u);
    EXPECT_EQ(depth2.GetBids()[0].price_, 100);
    EXPECT_EQ(depth2.GetBids()[1].price_, 99);
    ASSERT_EQ(depth2.GetAsks().size(), 2u);
    EXPECT_EQ(depth2.GetAsks()[0].price_, 101);
    EXPECT_EQ(depth2.GetAsks()[1].price_, 102);

    BookDepth depth5(5);
    depth5.ExtractFrom(book);
    ASSERT_EQ(depth5.GetBids().size(), 3u);
    ASSERT_EQ(depth5.GetAsks().size(), 3u);
}

TEST(MarketDataTest, BookDepthAggregatesQuantities) {
    OrderBook book;

    book.AddOrder(LimitGtc(1, Side::Buy, 100, 10));
    book.AddOrder(LimitGtc(2, Side::Buy, 100, 20));

    BookDepth depth(5);
    depth.ExtractFrom(book);

    ASSERT_EQ(depth.GetBids().size(), 1u);
    EXPECT_EQ(depth.GetBids()[0].price_, 100);
    EXPECT_EQ(depth.GetBids()[0].quantity_, 30u);
}

TEST(MarketDataTest, FullPipeline) {
    OrderBook book;
    MarketDataCollector collector;
    BookDepth depth(10);

    book.SetEventCallback([&](const OrderBookEvent& event) {
        collector.OnEvent(event);
    });

    book.AddOrder(LimitGtc(1, Side::Sell, 100, 20));
    book.AddOrder(LimitGtc(2, Side::Sell, 101, 30));
    book.AddOrder(LimitGtc(3, Side::Buy, 99, 25));

    EXPECT_EQ(collector.GetTradeCount(), 0u);
    EXPECT_EQ(collector.GetTotalVolume(), 0u);

    book.AddOrder(LimitGtc(4, Side::Buy, 100, 10));

    EXPECT_EQ(collector.GetTradeCount(), 1u);
    EXPECT_EQ(collector.GetTotalVolume(), 10u);

    depth.ExtractFrom(book);
    ASSERT_EQ(depth.GetBids().size(), 1u);
    ASSERT_EQ(depth.GetAsks().size(), 2u);
    EXPECT_EQ(depth.GetAsks()[0].quantity_, 10u);
    EXPECT_EQ(depth.GetAsks()[1].quantity_, 30u);

    const auto stats = collector.ComputeStats(book);
    ASSERT_TRUE(stats.bestBid_.has_value());
    ASSERT_TRUE(stats.bestAsk_.has_value());
    EXPECT_EQ(*stats.bestBid_, 99);
    EXPECT_EQ(*stats.bestAsk_, 100);
    EXPECT_EQ(stats.spread_, 1);
    EXPECT_EQ(stats.totalVolume_, 10u);
    EXPECT_EQ(stats.tradeCount_, 1u);
}

TEST(MarketDataTest, MarketOrderTradesRecorded) {
    OrderBook book;
    MarketDataCollector collector;

    book.SetEventCallback([&](const OrderBookEvent& event) {
        collector.OnEvent(event);
    });

    book.AddOrder(LimitGtc(1, Side::Sell, 100, 10));
    book.AddOrder(MarketOrder(2, Side::Buy, 5));

    EXPECT_EQ(collector.GetTradeCount(), 1u);
    EXPECT_EQ(collector.GetTotalVolume(), 5u);
    EXPECT_DOUBLE_EQ(collector.GetVWAP(), 100.0);
}
