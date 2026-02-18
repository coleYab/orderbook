#include <gtest/gtest.h>

#include "orderbook/order_book.h"

static auto LimitGtc(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillCancel, id, side, price, qty);
}

static auto LimitIoc(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::ImmediateOrCancel, id, side, price, qty);
}

static auto LimitFok(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::FillOrKill, id, side, price, qty);
}

static auto MarketOrder(OrderId id, Side side, Quantity qty) {
    return std::make_shared<Order>(OrderType::Market, TimeInForce::ImmediateOrCancel, id, side, 0, qty);
}

static auto StopLimitGtc(OrderId id, Side side, Price price, Quantity qty, Price stopPrice) {
    return std::make_shared<Order>(OrderType::StopLimit, TimeInForce::GoodTillCancel, id, side, price, qty, stopPrice);
}

static auto StopMarketOrder(OrderId id, Side side, Quantity qty, Price stopPrice) {
    return std::make_shared<Order>(OrderType::StopMarket, TimeInForce::ImmediateOrCancel, id, side, 0, qty, stopPrice);
}

static auto IcebergGtc(OrderId id, Side side, Price price, Quantity totalQty, Quantity peak) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillCancel, id, side, price, totalQty, 0, peak);
}

TEST(OrderBookTest, EmptyBookAddBuyOrder) {
    OrderBook book;
    const auto trades = book.AddOrder(LimitGtc(1, Side::Buy, 100, 10));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, EmptyBookAddSellOrder) {
    OrderBook book;
    const auto trades = book.AddOrder(LimitGtc(1, Side::Sell, 100, 10));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, BuySellSamePriceFullFill) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 10)).empty());
    const auto trades = book.AddOrder(LimitGtc(2, Side::Sell, 100, 10));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 10);
    EXPECT_EQ(trades[0].GetAskTrade().quantity_, 10);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, SellBuySamePriceFullFill) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 10)).empty());
    const auto trades = book.AddOrder(LimitGtc(2, Side::Buy, 100, 10));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, NoCrossPrice) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 99, 10)).empty());
    const auto trades = book.AddOrder(LimitGtc(2, Side::Sell, 100, 10));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 2);
}

TEST(OrderBookTest, PartialFillBuySell) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 10)).empty());
    const auto trades = book.AddOrder(LimitGtc(2, Side::Sell, 100, 4));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, PartialFillSellBuy) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 10)).empty());
    const auto trades = book.AddOrder(LimitGtc(2, Side::Buy, 100, 3));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, FifoPrioritySameLevel) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 10)).empty());
    ASSERT_TRUE(book.AddOrder(LimitGtc(2, Side::Buy, 100, 5)).empty());
    const auto trades = book.AddOrder(LimitGtc(3, Side::Sell, 100, 12));
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].GetBidTrade().orderId_, 1);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 10);
    EXPECT_EQ(trades[1].GetBidTrade().orderId_, 2);
    EXPECT_EQ(trades[1].GetBidTrade().quantity_, 2);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, BuyAggressiveAcrossLevels) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 5)).empty());
    ASSERT_TRUE(book.AddOrder(LimitGtc(2, Side::Sell, 101, 10)).empty());
    const auto trades = book.AddOrder(LimitGtc(3, Side::Buy, 101, 12));
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].GetAskTrade().orderId_, 1);
    EXPECT_EQ(trades[0].GetAskTrade().quantity_, 5);
    EXPECT_EQ(trades[1].GetAskTrade().orderId_, 2);
    EXPECT_EQ(trades[1].GetAskTrade().quantity_, 7);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, SellAggressiveAcrossLevels) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 5)).empty());
    ASSERT_TRUE(book.AddOrder(LimitGtc(2, Side::Buy, 99, 10)).empty());
    const auto trades = book.AddOrder(LimitGtc(3, Side::Sell, 99, 8));
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].GetBidTrade().orderId_, 1);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 5);
    EXPECT_EQ(trades[1].GetBidTrade().orderId_, 2);
    EXPECT_EQ(trades[1].GetBidTrade().quantity_, 3);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, IocNoMatchCancels) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 10)).empty());
    const auto trades = book.AddOrder(LimitIoc(2, Side::Buy, 99, 10));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, IocPartialFillCancelsRest) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 3)).empty());
    const auto trades = book.AddOrder(LimitIoc(2, Side::Buy, 100, 10));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 3);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, IocFullFill) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 10)).empty());
    const auto trades = book.AddOrder(LimitIoc(2, Side::Buy, 100, 10));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 10);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, FokFullFill) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 10)).empty());
    const auto trades = book.AddOrder(LimitFok(2, Side::Buy, 100, 10));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, FokPartialFillRejected) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 3)).empty());
    const auto trades = book.AddOrder(LimitFok(2, Side::Buy, 100, 10));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, FokNoMatchRejected) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 10)).empty());

    const auto trades = book.AddOrder(LimitFok(2, Side::Buy, 99, 5));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, FokInsufficientLiquidityRejected) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 3)).empty());

    const auto trades = book.AddOrder(LimitFok(2, Side::Buy, 100, 5));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, FokAcrossMultipleLevels) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 5)).empty());
    ASSERT_TRUE(book.AddOrder(LimitGtc(2, Side::Sell, 101, 5)).empty());
    const auto trades = book.AddOrder(LimitFok(3, Side::Buy, 101, 10));
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].GetAskTrade().quantity_, 5);
    EXPECT_EQ(trades[1].GetAskTrade().quantity_, 5);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, MarketBuyAgainstAsks) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 5)).empty());
    ASSERT_TRUE(book.AddOrder(LimitGtc(2, Side::Sell, 101, 5)).empty());
    const auto trades = book.AddOrder(MarketOrder(3, Side::Buy, 8));
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[0].GetAskTrade().orderId_, 1);
    EXPECT_EQ(trades[0].GetAskTrade().quantity_, 5);
    EXPECT_EQ(trades[0].GetAskTrade().price_, 100);
    EXPECT_EQ(trades[1].GetAskTrade().orderId_, 2);
    EXPECT_EQ(trades[1].GetAskTrade().quantity_, 3);
    EXPECT_EQ(trades[1].GetAskTrade().price_, 101);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, MarketSellAgainstBids) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 5)).empty());
    ASSERT_TRUE(book.AddOrder(LimitGtc(2, Side::Buy, 99, 5)).empty());
    const auto trades = book.AddOrder(MarketOrder(3, Side::Sell, 8));
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, MarketBuyNoLiquidity) {
    OrderBook book;
    const auto trades = book.AddOrder(MarketOrder(1, Side::Buy, 10));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, MarketSellNoLiquidity) {
    OrderBook book;
    const auto trades = book.AddOrder(MarketOrder(1, Side::Sell, 10));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, MarketBuyPartialFill) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 3)).empty());
    const auto trades = book.AddOrder(MarketOrder(2, Side::Buy, 10));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].GetAskTrade().quantity_, 3);
    EXPECT_EQ(trades[0].GetBidTrade().quantity_, 3);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, StopLimitBuyTriggersOnPriceRise) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 10)).empty());
    ASSERT_TRUE(book.AddOrder(StopLimitGtc(2, Side::Buy, 100, 5, 99)).empty());
    EXPECT_EQ(book.Size(), 2);

    const auto trades = book.AddOrder(LimitGtc(3, Side::Buy, 100, 1));

    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, StopLimitSellTriggersOnPriceFall) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 10)).empty());
    ASSERT_TRUE(book.AddOrder(StopLimitGtc(2, Side::Sell, 99, 5, 100)).empty());
    EXPECT_EQ(book.Size(), 2);

    const auto trades = book.AddOrder(LimitGtc(3, Side::Sell, 100, 1));
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, StopDoesNotTriggerBelowPrice) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 98, 5)).empty());
    ASSERT_TRUE(book.AddOrder(StopLimitGtc(2, Side::Buy, 100, 5, 99)).empty());
    EXPECT_EQ(book.Size(), 2);

    const auto trades = book.AddOrder(LimitGtc(3, Side::Buy, 98, 5));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, CancelStopOrder) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(StopLimitGtc(1, Side::Buy, 100, 5, 99)).empty());
    EXPECT_EQ(book.Size(), 1);
    book.CancelOrder(1);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, StopMarketBuyTriggers) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 10)).empty());
    ASSERT_TRUE(book.AddOrder(StopMarketOrder(2, Side::Buy, 5, 99)).empty());
    EXPECT_EQ(book.Size(), 2);

    const auto trades = book.AddOrder(LimitGtc(3, Side::Buy, 100, 1));
    ASSERT_EQ(trades.size(), 2);
    EXPECT_EQ(trades[1].GetBidTrade().orderId_, 2);
}

TEST(OrderBookTest, IcebergShowsPeakInLevelInfos) {
    OrderBook book;

    ASSERT_TRUE(book.AddOrder(IcebergGtc(1, Side::Sell, 100, 100, 10)).empty());
    const auto infos = book.GetOrderLevelInfos();
    ASSERT_EQ(infos.GetAsks().size(), 1);
    EXPECT_EQ(infos.GetAsks()[0].quantity_, 10);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, IcebergRefillsPeak) {
    OrderBook book;

    ASSERT_TRUE(book.AddOrder(IcebergGtc(1, Side::Sell, 100, 20, 5)).empty());
    EXPECT_EQ(book.Size(), 1);

    const auto trades = book.AddOrder(MarketOrder(2, Side::Buy, 20));
    ASSERT_EQ(trades.size(), 4);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, IcebergPartialPeakRefill) {
    OrderBook book;

    ASSERT_TRUE(book.AddOrder(IcebergGtc(1, Side::Sell, 100, 7, 5)).empty());

    const auto trades = book.AddOrder(MarketOrder(2, Side::Buy, 5));
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(book.Size(), 1);

    const auto infos = book.GetOrderLevelInfos();
    ASSERT_EQ(infos.GetAsks().size(), 1);
    EXPECT_EQ(infos.GetAsks()[0].quantity_, 2);
}

TEST(OrderBookTest, IcebergCancelRemaining) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(IcebergGtc(1, Side::Buy, 100, 50, 10)).empty());
    EXPECT_EQ(book.Size(), 1);

    book.CancelOrder(1);
    EXPECT_EQ(book.Size(), 0);
    EXPECT_TRUE(book.GetOrderLevelInfos().GetBids().empty());
}

TEST(OrderBookTest, GtdWithPastExpiryRejected) {
    OrderBook book;
    const auto past = std::chrono::system_clock::now() - std::chrono::hours(1);
    auto order = std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillDate, 1, Side::Buy, 100, 10, 0, 0, past);
    const auto trades = book.AddOrder(order);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, GtdWithFutureExpiryAccepted) {
    OrderBook book;
    const auto future = std::chrono::system_clock::now() + std::chrono::hours(1);
    auto order = std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillDate, 1, Side::Buy, 100, 10, 0, 0, future);
    const auto trades = book.AddOrder(order);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, GtdExpiredOrderSweptOnNextAdd) {
    OrderBook book;
    const auto past = std::chrono::system_clock::now() - std::chrono::hours(1);
    auto order = std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillDate, 1, Side::Buy, 100, 10, 0, 0, past);

    const auto trades = book.AddOrder(order);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, CancelExistingOrder) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 10)).empty());
    book.CancelOrder(1);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, CancelNonexistentOrder) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 10)).empty());
    book.CancelOrder(999);
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, ModifyOrderPrice) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 99, 10)).empty());
    auto trades = book.ModifyOrder(OrderModify{1, Side::Buy, 101, 10});
    ASSERT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, ModifyOrderTriggersMatch) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Sell, 100, 5)).empty());
    ASSERT_TRUE(book.AddOrder(LimitGtc(2, Side::Buy, 99, 5)).empty());
    auto trades = book.ModifyOrder(OrderModify{2, Side::Buy, 100, 5});
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, ModifyNonexistentOrder) {
    OrderBook book;
    auto trades = book.ModifyOrder(OrderModify{999, Side::Buy, 100, 10});
    EXPECT_TRUE(trades.empty());
}

TEST(OrderBookTest, RejectDuplicateOrderId) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 10)).empty());
    auto trades = book.AddOrder(LimitGtc(1, Side::Sell, 100, 10));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.Size(), 1);
}

TEST(OrderBookTest, LevelInfosEmpty) {
    const OrderBook book;
    const auto infos = book.GetOrderLevelInfos();
    EXPECT_TRUE(infos.GetBids().empty());
    EXPECT_TRUE(infos.GetAsks().empty());
}

TEST(OrderBookTest, LevelInfosAggregate) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 10)).empty());
    ASSERT_TRUE(book.AddOrder(LimitGtc(2, Side::Buy, 100, 5)).empty());
    ASSERT_TRUE(book.AddOrder(LimitGtc(3, Side::Buy, 99, 20)).empty());

    const auto infos = book.GetOrderLevelInfos();
    ASSERT_EQ(infos.GetBids().size(), 2);
    EXPECT_EQ(infos.GetBids()[0].price_, 100);
    EXPECT_EQ(infos.GetBids()[0].quantity_, 15);
    EXPECT_EQ(infos.GetBids()[1].price_, 99);
    EXPECT_EQ(infos.GetBids()[1].quantity_, 20);
}

TEST(OrderBookTest, LevelInfosAfterPartialFill) {
    OrderBook book;
    ASSERT_TRUE(book.AddOrder(LimitGtc(1, Side::Buy, 100, 10)).empty());
    book.AddOrder(LimitGtc(2, Side::Sell, 100, 4));

    const auto infos = book.GetOrderLevelInfos();
    ASSERT_EQ(infos.GetBids().size(), 1);
    EXPECT_EQ(infos.GetBids()[0].quantity_, 6);
    EXPECT_TRUE(infos.GetAsks().empty());
}

TEST(OrderBookTest, FullMatchClearsAll) {
    OrderBook book;
    for (OrderId i = 1; i <= 5; ++i) {
        book.AddOrder(LimitGtc(i, Side::Buy, 100, 10));
    }
    for (OrderId i = 6; i <= 10; ++i) {
        book.AddOrder(LimitGtc(i, Side::Sell, 100, 10));
    }
    EXPECT_EQ(book.Size(), 0);
}

TEST(OrderBookTest, StressManyOrders) {
    constexpr OrderId kNumOrders = 10'000;
    OrderBook book;

    for (OrderId i = 0; i < kNumOrders; ++i) {
        const auto side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        book.AddOrder(LimitGtc(i, side, 100, 1));
    }
    ASSERT_EQ(book.Size(), 0);

    for (OrderId i = kNumOrders; i < 2 * kNumOrders; ++i) {
        book.AddOrder(LimitGtc(i, Side::Buy, 100, 1));
    }
    EXPECT_EQ(book.Size(), kNumOrders);
}
