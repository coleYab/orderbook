#include "orderbook/order.h"

#include <format>
#include <stdexcept>

Order::Order(OrderType type, TimeInForce tif, OrderId orderId,
             Side side, Price price, Quantity quantity,
             Price stopPrice, Quantity peakSize, TimePoint expiry)
    : type_{type}
    , tif_{tif}
    , orderId_{orderId}
    , side_{side}
    , price_{price}
    , initialQuantity_{quantity}
    , remainingQuantity_{peakSize > 0 ? std::min(peakSize, quantity) : quantity}
    , stopPrice_{stopPrice}
    , peakSize_{peakSize}
    , remainingHidden_{peakSize > 0 ? quantity - std::min(peakSize, quantity) : Quantity{0}}
    , expiry_{expiry}
{}

OrderId Order::GetOrderId() const { return orderId_; }
OrderType Order::GetType() const { return type_; }
TimeInForce Order::GetTimeInForce() const { return tif_; }
Side Order::GetSide() const { return side_; }
Price Order::GetPrice() const { return price_; }
Quantity Order::GetInitialQuantity() const { return initialQuantity_; }
Quantity Order::GetRemainingQuantity() const { return remainingQuantity_; }
Quantity Order::GetFilledQuantity() const { return initialQuantity_ - remainingQuantity_; }
bool Order::IsFilled() const { return remainingQuantity_ == 0; }

Price Order::GetStopPrice() const { return stopPrice_; }
Quantity Order::GetPeakSize() const { return peakSize_; }
Quantity Order::GetRemainingHidden() const { return remainingHidden_; }
TimePoint Order::GetExpiry() const { return expiry_; }

bool Order::IsStop() const {
    return type_ == OrderType::StopLimit || type_ == OrderType::StopMarket;
}

bool Order::IsMarket() const {
    return type_ == OrderType::Market || type_ == OrderType::StopMarket;
}

bool Order::IsIceberg() const {
    return peakSize_ > 0;
}

bool Order::IsExpired() const {
    return tif_ == TimeInForce::GoodTillDate && expiry_ <= std::chrono::system_clock::now();
}

bool Order::HasHiddenRemaining() const {
    return remainingHidden_ > 0;
}

void Order::Fill(Quantity quantity) {
    if (quantity > GetRemainingQuantity()) {
        throw std::logic_error{
            std::format("Order [{}] cannot be filled for more than its remaining quantity.", GetOrderId())
        };
    }
    remainingQuantity_ -= quantity;
}

OrderPointer CreateTriggerOrder(const Order& order) {
    if (order.GetType() == OrderType::StopLimit) {
        return std::make_shared<Order>(
            OrderType::Limit, TimeInForce::GoodTillCancel,
            order.GetOrderId(), order.GetSide(), order.GetPrice(), order.GetInitialQuantity()
        );
    }
    return std::make_shared<Order>(
        OrderType::Market, TimeInForce::ImmediateOrCancel,
        order.GetOrderId(), order.GetSide(), Price{0}, order.GetInitialQuantity()
    );
}

void Order::ReplenishPeak() {
    const Quantity refill = std::min(peakSize_, remainingHidden_);
    remainingQuantity_ = refill;
    remainingHidden_ -= refill;
}

OrderModify::OrderModify(OrderId orderId, Side side, Price price,
                         Quantity quantity)
    : orderId_{orderId}
    , side_{side}
    , price_{price}
    , quantity_{quantity}
{}

OrderId OrderModify::GetOrderId() const { return orderId_; }
Side OrderModify::GetSide() const { return side_; }
Price OrderModify::GetPrice() const { return price_; }
Quantity OrderModify::GetQuantity() const { return quantity_; }

OrderPointer OrderModify::ToOrderPointer(const Order& existing) const {
    return std::make_shared<Order>(
        existing.GetType(),
        existing.GetTimeInForce(),
        GetOrderId(), GetSide(), GetPrice(), GetQuantity(),
        existing.GetStopPrice(),
        existing.GetPeakSize(),
        existing.GetExpiry()
    );
}
