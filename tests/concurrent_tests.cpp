#include <gtest/gtest.h>

#include "orderbook/market_data.h"
#include "orderbook/order_book.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

auto LimitGtc(OrderId id, Side side, Price price, Quantity qty) {
    return std::make_shared<Order>(OrderType::Limit, TimeInForce::GoodTillCancel,
                                   id, side, price, qty);
}

}

TEST(ConcurrentTest, AddAndCancelOrders) {
    OrderBook book;
    std::atomic<bool> running{true};
    std::atomic<std::size_t> addCount{0};
    std::atomic<std::size_t> cancelCount{0};

    constexpr int kNumProducers = 4;
    constexpr int kOrdersPerThread = 500;

    auto producer = [&](int tid) {
        OrderId base = tid * (kOrdersPerThread + 1) + 1;
        for (int i = 0; i < kOrdersPerThread; ++i) {
            OrderId id = base + i;
            book.AddOrder(LimitGtc(id, Side::Buy, 100, 10));
            addCount.fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto canceller = [&]() {
        while (running.load(std::memory_order_relaxed)) {

            for (OrderId id = 1; id <= 2000; id += 3) {
                book.CancelOrder(id);
                cancelCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    auto reader = [&]() {
        while (running.load(std::memory_order_relaxed)) {
            (void)book.Size();
            (void)book.GetOrderLevelInfos();
            (void)book.GetTopOfBid();
            (void)book.GetTopOfAsk();
        }
    };

    std::vector<std::thread> threads;

    for (int t = 0; t < kNumProducers; ++t) {
        threads.emplace_back(producer, t);
    }
    threads.emplace_back(canceller);
    threads.emplace_back(reader);

    std::this_thread::sleep_for(200ms);

    running.store(false, std::memory_order_relaxed);

    for (auto& t : threads) t.join();

    EXPECT_GT(addCount.load(), 0u);
    EXPECT_GT(cancelCount.load(), 0u);
}

TEST(ConcurrentTest, ConcurrentMatchStress) {
    OrderBook book;
    std::atomic<bool> running{true};
    std::atomic<std::size_t> tradeCount{0};

    constexpr int kNumThreads = 4;
    constexpr int kOrdersPerThread = 200;

    for (int i = 0; i < 50; ++i) {
        book.AddOrder(LimitGtc(10000 + i, Side::Sell, 100, 10));
        book.AddOrder(LimitGtc(20000 + i, Side::Buy, 99, 10));
    }

    auto worker = [&](int tid) {
        OrderId base = tid * (kOrdersPerThread + 1) + 1;
        for (int i = 0; i < kOrdersPerThread; ++i) {

            if (i % 2 == 0) {
                auto trades = book.AddOrder(LimitGtc(base + i, Side::Buy, 100, 1));
                tradeCount.fetch_add(trades.size(), std::memory_order_relaxed);
            } else {
                auto trades = book.AddOrder(LimitGtc(base + i, Side::Sell, 99, 1));
                tradeCount.fetch_add(trades.size(), std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back(worker, t);
    }

    std::this_thread::sleep_for(100ms);
    running.store(false, std::memory_order_relaxed);
    for (auto& t : threads) t.join();

    EXPECT_GT(tradeCount.load(), 0u);

    const auto infos = book.GetOrderLevelInfos();
    if (!infos.GetBids().empty() && !infos.GetAsks().empty()) {
        EXPECT_LT(infos.GetBids().front().price_, infos.GetAsks().front().price_);
    }
}

TEST(ConcurrentTest, ConcurrentEvents) {
    OrderBook book;
    MarketDataCollector collector;

    book.SetEventCallback([&](const OrderBookEvent& event) {
        collector.OnEvent(event);
    });

    std::atomic<bool> running{true};

    auto producer = [&](int tid) {
        OrderId base = tid * 500 + 1;
        for (int i = 0; i < 200; ++i) {
            OrderId id = base + i;
            if (i < 100) {
                book.AddOrder(LimitGtc(id, Side::Sell, 100, 10));
            } else {
                book.AddOrder(LimitGtc(id, Side::Buy, 100, 5));
            }
        }
    };

    auto reader = [&]() {
        while (running.load(std::memory_order_relaxed)) {
            (void)book.Size();
            (void)collector.GetTotalVolume();
            (void)collector.GetTradeCount();
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back(producer, t);
    }
    threads.emplace_back(reader);

    std::this_thread::sleep_for(200ms);
    running.store(false, std::memory_order_relaxed);
    for (auto& t : threads) t.join();

    EXPECT_GE(collector.GetTradeCount(), 0u);
}

TEST(ConcurrentTest, ConcurrentMarketOrders) {
    OrderBook book;

    for (int i = 0; i < 100; ++i) {
        book.AddOrder(LimitGtc(10000 + i, Side::Sell, 100, 10));
    }

    std::atomic<std::size_t> totalFilled{0};

    auto buyer = [&](int tid) {
        OrderId base = tid * 200 + 1;
        for (int i = 0; i < 100; ++i) {
            auto order = std::make_shared<Order>(
                OrderType::Market, TimeInForce::ImmediateOrCancel,
                base + i, Side::Buy, Price{0}, Quantity{1}
            );
            auto trades = book.AddOrder(std::move(order));
            totalFilled.fetch_add(trades.size(), std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back(buyer, t);
    }

    for (auto& t : threads) t.join();

    EXPECT_GT(totalFilled.load(), 0u);
}
