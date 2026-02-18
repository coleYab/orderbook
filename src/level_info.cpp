#include "orderbook/level_info.h"

OrderBookLevelInfos::OrderBookLevelInfos(LevelInfos bids, LevelInfos asks)
    : bids_{std::move(bids)}
    , asks_{std::move(asks)}
{}

const LevelInfos& OrderBookLevelInfos::GetBids() const {
    return bids_;
}

const LevelInfos& OrderBookLevelInfos::GetAsks() const {
    return asks_;
}
