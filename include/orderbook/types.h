#pragma once

#include <chrono>
#include <cstdint>

enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Limit,
    Market,
    StopLimit,
    StopMarket
};

enum class TimeInForce {
    GoodTillCancel,
    ImmediateOrCancel,
    FillOrKill,
    GoodTillDate
};

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::int64_t;
using StockId = std::int32_t;
using TimePoint = std::chrono::system_clock::time_point;
