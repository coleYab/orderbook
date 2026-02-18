#pragma once

#include "types.h"

#include <vector>

struct LevelInfo {
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;

class OrderBookLevelInfos {
public:
    OrderBookLevelInfos() = default;
    OrderBookLevelInfos(LevelInfos bids, LevelInfos asks);

    [[nodiscard]] const LevelInfos& GetBids() const;
    [[nodiscard]] const LevelInfos& GetAsks() const;

private:
    LevelInfos bids_;
    LevelInfos asks_;
};
