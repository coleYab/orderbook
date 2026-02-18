#pragma once

#include "types.h"

#include <list>
#include <memory>

class Order {
public:
    Order(OrderType type, TimeInForce tif, OrderId orderId, Side side,
          Price price, Quantity quantity,
          Price stopPrice = 0, Quantity peakSize = 0,
          TimePoint expiry = TimePoint::max());

    [[nodiscard]] OrderId GetOrderId() const;
    [[nodiscard]] OrderType GetType() const;
    [[nodiscard]] TimeInForce GetTimeInForce() const;
    [[nodiscard]] Side GetSide() const;
    [[nodiscard]] Price GetPrice() const;
    [[nodiscard]] Quantity GetInitialQuantity() const;
    [[nodiscard]] Quantity GetRemainingQuantity() const;
    [[nodiscard]] Quantity GetFilledQuantity() const;
    [[nodiscard]] bool IsFilled() const;

    [[nodiscard]] Price GetStopPrice() const;
    [[nodiscard]] Quantity GetPeakSize() const;
    [[nodiscard]] Quantity GetRemainingHidden() const;
    [[nodiscard]] TimePoint GetExpiry() const;
    [[nodiscard]] bool IsStop() const;
    [[nodiscard]] bool IsMarket() const;
    [[nodiscard]] bool IsIceberg() const;
    [[nodiscard]] bool IsExpired() const;
    [[nodiscard]] bool HasHiddenRemaining() const;

    void Fill(Quantity quantity);
    void ReplenishPeak();

private:
    OrderType type_;
    TimeInForce tif_;
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity initialQuantity_;
    Quantity remainingQuantity_;

    Price stopPrice_;
    Quantity peakSize_;
    Quantity remainingHidden_;
    TimePoint expiry_;
};

using OrderPointer = std::shared_ptr<Order>;
using OrderPointers = std::list<OrderPointer>;

[[nodiscard]] OrderPointer CreateTriggerOrder(const Order& order);

class OrderModify {
public:
    OrderModify(OrderId orderId, Side side, Price price, Quantity quantity);

    [[nodiscard]] OrderId GetOrderId() const;
    [[nodiscard]] Side GetSide() const;
    [[nodiscard]] Price GetPrice() const;
    [[nodiscard]] Quantity GetQuantity() const;

    [[nodiscard]] OrderPointer ToOrderPointer(const Order& existing) const;

private:
    OrderId orderId_;
    Side side_;
    Price price_;
    Quantity quantity_;
};
