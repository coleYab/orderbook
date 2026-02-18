#pragma once

#include "types.h"

#include <vector>

struct TradeInfo {
    OrderId orderId_;
    Price price_;
    Quantity quantity_;
};

class Trade {
public:
    Trade(TradeInfo bidTrade, TradeInfo askTrade);

    [[nodiscard]] const TradeInfo& GetBidTrade() const;
    [[nodiscard]] const TradeInfo& GetAskTrade() const;

private:
    TradeInfo bidTrade_;
    TradeInfo askTrade_;
};

using Trades = std::vector<Trade>;
