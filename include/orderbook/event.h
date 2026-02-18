#pragma once

#include "order.h"
#include "types.h"

#include <functional>
#include <variant>
#include <vector>

struct OrderAddedEvent {
    OrderPointer order_{};
};

struct OrderRejectedEvent {
    OrderPointer order_{};
    const char* reason_{};
};

struct TradeEvent {
    OrderPointer bidOrder_{};
    OrderPointer askOrder_{};
    Price price_{};
    Quantity quantity_{};
};

struct OrderFilledEvent {
    OrderPointer order_{};
};

struct OrderCancelledEvent {
    OrderPointer order_{};
};

struct OrderExpiredEvent {
    OrderPointer order_{};
};

struct StopPlacedEvent {
    OrderPointer order_{};
};

struct StopTriggeredEvent {
    OrderPointer order_{};
    OrderPointer triggerOrder_{};
};

using OrderBookEvent = std::variant<
    OrderAddedEvent,
    OrderRejectedEvent,
    TradeEvent,
    OrderFilledEvent,
    OrderCancelledEvent,
    OrderExpiredEvent,
    StopPlacedEvent,
    StopTriggeredEvent
>;

using EventCallback = std::function<void(const OrderBookEvent&)>;
