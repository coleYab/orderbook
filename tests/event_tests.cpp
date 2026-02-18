#include <gtest/gtest.h>

#include "orderbook/matching_engine.h"
#include "orderbook/order_book.h"

#include <vector>

using namespace std::chrono_literals;

namespace {

auto LimitGtc(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillCancel,
                                   id, side, price, qty);
}

auto LimitIoc(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::ImmediateOrCancel,
                                   id, side, price, qty);
}

auto LimitFok(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::FillOrKill,
                                   id, side, price, qty);
}

auto MarketOrder(OrderId id, Side side, Quantity qty) {
    return std::make_shared<Order>(OrderType::Market, TimeInForce::ImmediateOrCancel,
                                   id, side, Price{0}, qty);
}

auto StopLimitGtc(OrderId id, Side side, Price price, Quantity qty, Price stopPrice) {
    return std::make_shared<Order>(OrderType::StopLimit, TimeInForce::GoodTillCancel,
                                   id, side, price, qty, stopPrice);
}

auto ExpiredGtd(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillDate,
                                   id, side, price, qty,
                                   Price{0}, Quantity{0},
                                   std::chrono::system_clock::now() - 1h);
}

}

struct EventCollector {
    std::vector<OrderBookEvent> events;

    void operator()(const OrderBookEvent& event) {
        events.push_back(event);
    }

    template <typename T>
    [[nodiscard]] std::size_t Count() const {
        std::size_t n = 0;
        for (const auto& e : events) {
            if (std::holds_alternative<T>(e)) ++n;
        }
        return n;
    }

    template <typename T>
    [[nodiscard]] const T& Get(std::size_t n = 0) const {
        std::size_t seen = 0;
        for (const auto& e : events) {
            if (auto* p = std::get_if<T>(&e)) {
                if (seen == n) return *p;
                ++seen;
            }
        }
        static T dummy{nullptr};
        return dummy;
    }
};

TEST(EventTest, OrderAddedOnRestingLimit) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Buy, 100, 10));

    ASSERT_EQ(collector.Count<OrderAddedEvent>(), 1);
    EXPECT_EQ(collector.Get<OrderAddedEvent>().order_->GetOrderId(), 1);
}

TEST(EventTest, OrderAddedOnNonMatchingIoc) {

    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitIoc(1, Side::Buy, 100, 10));

    EXPECT_EQ(collector.Count<OrderAddedEvent>(), 0);
    EXPECT_EQ(collector.Count<OrderRejectedEvent>(), 1);
}

TEST(EventTest, RejectedOnDuplicateId) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Buy, 100, 10));
    book.AddOrder(LimitGtc(1, Side::Buy, 100, 5));

    EXPECT_EQ(collector.Count<OrderAddedEvent>(), 1);
    EXPECT_EQ(collector.Count<OrderRejectedEvent>(), 1);
    EXPECT_STREQ(collector.Get<OrderRejectedEvent>().reason_, "duplicate order id");
}

TEST(EventTest, RejectedOnExpiredGtd) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(ExpiredGtd(1, Side::Buy, 100, 10));

    EXPECT_EQ(collector.Count<OrderRejectedEvent>(), 1);
    EXPECT_STREQ(collector.Get<OrderRejectedEvent>().reason_, "order expired");
}

TEST(EventTest, RejectedOnFokCannotFill) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitFok(1, Side::Buy, 100, 10));

    EXPECT_EQ(collector.Count<OrderRejectedEvent>(), 1);
    EXPECT_STREQ(collector.Get<OrderRejectedEvent>().reason_, "cannot fully fill");
}

TEST(EventTest, RejectedOnMarketNoLiquidity) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(MarketOrder(1, Side::Buy, 10));

    EXPECT_EQ(collector.Count<OrderRejectedEvent>(), 1);
    EXPECT_STREQ(collector.Get<OrderRejectedEvent>().reason_, "no liquidity");
}

TEST(EventTest, RejectedOnIocCannotMatch) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitIoc(1, Side::Buy, 100, 10));

    EXPECT_EQ(collector.Count<OrderRejectedEvent>(), 1);
    EXPECT_STREQ(collector.Get<OrderRejectedEvent>().reason_, "cannot match");
}

TEST(EventTest, TradeEmittedOnMatch) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Sell, 100, 10));
    book.AddOrder(LimitGtc(2, Side::Buy, 100, 5));

    ASSERT_EQ(collector.Count<TradeEvent>(), 1);
    const auto& trade = collector.Get<TradeEvent>();
    EXPECT_EQ(trade.bidOrder_->GetOrderId(), 2);
    EXPECT_EQ(trade.askOrder_->GetOrderId(), 1);
    EXPECT_EQ(trade.price_, 100);
    EXPECT_EQ(trade.quantity_, 5);
}

TEST(EventTest, MultipleTradesOnPartialFill) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Sell, 100, 10));
    book.AddOrder(LimitGtc(2, Side::Buy, 100, 4));
    book.AddOrder(LimitGtc(3, Side::Buy, 100, 4));

    ASSERT_EQ(collector.Count<TradeEvent>(), 2);
    EXPECT_EQ(collector.Get<TradeEvent>(0).quantity_, 4);
    EXPECT_EQ(collector.Get<TradeEvent>(1).quantity_, 4);
}

TEST(EventTest, FilledEmittedWhenOrderConsumed) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Sell, 100, 5));
    book.AddOrder(LimitGtc(2, Side::Buy, 100, 5));

    ASSERT_EQ(collector.Count<OrderFilledEvent>(), 2);
}

TEST(EventTest, FilledNotEmittedOnPartialFill) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Sell, 100, 10));
    book.AddOrder(LimitGtc(2, Side::Buy, 99, 5));

    ASSERT_EQ(collector.Count<OrderFilledEvent>(), 0);
}

TEST(EventTest, CancelledEmittedOnCancel) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Buy, 100, 10));
    book.CancelOrder(1);

    ASSERT_EQ(collector.Count<OrderCancelledEvent>(), 1);
    EXPECT_EQ(collector.Get<OrderCancelledEvent>().order_->GetOrderId(), 1);
}

TEST(EventTest, CancelledEmittedOnStopCancel) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(StopLimitGtc(1, Side::Buy, 100, 10, 110));
    book.CancelOrder(1);

    ASSERT_EQ(collector.Count<OrderCancelledEvent>(), 1);
    EXPECT_EQ(collector.Get<OrderCancelledEvent>().order_->GetOrderId(), 1);
}

TEST(EventTest, CancelNonexistentEmitsNothing) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.CancelOrder(999);
    EXPECT_EQ(collector.Count<OrderCancelledEvent>(), 0);
}

TEST(EventTest, StopPlacedOnStopAdd) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(StopLimitGtc(1, Side::Buy, 100, 10, 110));

    ASSERT_EQ(collector.Count<StopPlacedEvent>(), 1);
    EXPECT_EQ(collector.Get<StopPlacedEvent>().order_->GetOrderId(), 1);
}

TEST(EventTest, StopTriggeredOnPriceCross) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Sell, 100, 10));
    book.AddOrder(StopLimitGtc(2, Side::Buy, 100, 5, 99));

    book.AddOrder(LimitGtc(3, Side::Buy, 100, 1));

    ASSERT_EQ(collector.Count<StopTriggeredEvent>(), 1);
    const auto& stopEvent = collector.Get<StopTriggeredEvent>();
    EXPECT_EQ(stopEvent.order_->GetOrderId(), 2);
    EXPECT_EQ(stopEvent.triggerOrder_->GetType(), OrderType::Limit);
}

TEST(EventTest, TopOfBidAndAsk) {
    OrderBook book;

    EXPECT_FALSE(book.GetTopOfBid().has_value());
    EXPECT_FALSE(book.GetTopOfAsk().has_value());

    book.AddOrder(LimitGtc(1, Side::Buy, 100, 5));
    book.AddOrder(LimitGtc(2, Side::Buy, 99, 3));
    book.AddOrder(LimitGtc(3, Side::Sell, 101, 4));

    ASSERT_TRUE(book.GetTopOfBid().has_value());
    EXPECT_EQ(book.GetTopOfBid()->price_, 100);
    EXPECT_EQ(book.GetTopOfBid()->quantity_, 5);

    ASSERT_TRUE(book.GetTopOfAsk().has_value());
    EXPECT_EQ(book.GetTopOfAsk()->price_, 101);
    EXPECT_EQ(book.GetTopOfAsk()->quantity_, 4);
}

TEST(EventTest, MatchingEngineForwardsEvents) {
    MatchingEngine engine;
    EventCollector collector;
    engine.SetEventCallback(std::ref(collector));

    engine.PlaceOrder(1, LimitGtc(1, Side::Buy, 100, 10));
    engine.PlaceOrder(1, LimitGtc(2, Side::Sell, 100, 5));

    ASSERT_EQ(collector.Count<OrderAddedEvent>(), 2);
    ASSERT_EQ(collector.Count<TradeEvent>(), 1);
    ASSERT_EQ(collector.Count<OrderFilledEvent>(), 1);
}

TEST(EventTest, ModifyEmitsCancelAndAdd) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Buy, 100, 10));
    book.ModifyOrder(OrderModify(1, Side::Buy, 101, 10));

    EXPECT_EQ(collector.Count<OrderCancelledEvent>(), 1);
    EXPECT_EQ(collector.Count<OrderAddedEvent>(), 2);
}

TEST(EventTest, IocRemainderCancelled) {
    OrderBook book;
    EventCollector collector;
    book.SetEventCallback(std::ref(collector));

    book.AddOrder(LimitGtc(1, Side::Sell, 100, 10));

    book.AddOrder(LimitIoc(2, Side::Buy, 100, 12));

    ASSERT_EQ(collector.Count<OrderAddedEvent>(), 2);
    ASSERT_GE(collector.Count<TradeEvent>(), 1);
    ASSERT_GE(collector.Count<OrderFilledEvent>(), 1);

    ASSERT_GE(collector.Count<OrderCancelledEvent>(), 1);
}
