#include "orderbook/trade.h"

Trade::Trade(TradeInfo bidTrade, TradeInfo askTrade)
    : bidTrade_{std::move(bidTrade)}
    , askTrade_{std::move(askTrade)}
{}

const TradeInfo& Trade::GetBidTrade() const { return bidTrade_; }
const TradeInfo& Trade::GetAskTrade() const { return askTrade_; }
