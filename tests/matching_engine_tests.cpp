#include <gtest/gtest.h>

#include "orderbook/matching_engine.h"

static auto LimitGtc(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillCancel, id, side, price, qty);
}

TEST(MatchingEngineTest, PlaceOrderAutoCreatesStock) {
    MatchingEngine engine;
    const auto result = engine.PlaceOrder(1, LimitGtc(1, Side::Buy, 100, 10));
    EXPECT_TRUE(result.trades_.empty());
    EXPECT_EQ(result.stockId_, 1);
}

TEST(MatchingEngineTest, MatchAcrossStocks) {
    MatchingEngine engine;
    ASSERT_TRUE(engine.PlaceOrder(1, LimitGtc(1, Side::Buy, 100, 10)).trades_.empty());
    const auto result = engine.PlaceOrder(1, LimitGtc(2, Side::Sell, 100, 10));
    ASSERT_EQ(result.trades_.size(), 1);
    EXPECT_EQ(result.stockId_, 1);
}

TEST(MatchingEngineTest, DifferentStocksDontMatch) {
    MatchingEngine engine;
    ASSERT_TRUE(engine.PlaceOrder(1, LimitGtc(1, Side::Buy, 100, 10)).trades_.empty());
    const auto result = engine.PlaceOrder(2, LimitGtc(2, Side::Sell, 100, 10));
    EXPECT_TRUE(result.trades_.empty());
    EXPECT_EQ(result.stockId_, 2);
}

TEST(MatchingEngineTest, CancelAcrossStocks) {
    MatchingEngine engine;
    engine.PlaceOrder(1, LimitGtc(1, Side::Buy, 100, 10));
    engine.PlaceOrder(2, LimitGtc(2, Side::Buy, 100, 10));
    engine.CancelOrder(1, 1);
    engine.CancelOrder(2, 2);
}

TEST(MatchingEngineTest, CancelNonexistentStock) {
    MatchingEngine engine;
    engine.CancelOrder(999, 1);
}
