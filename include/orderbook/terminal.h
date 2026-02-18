#pragma once

#include "orderbook/types.h"

#include <termios.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace style {
    constexpr auto R    = "\033[0m";
    constexpr auto B    = "\033[1m";
    constexpr auto D    = "\033[2m";

    constexpr auto BR   = "\033[91m";
    constexpr auto BG   = "\033[92m";
    constexpr auto BY   = "\033[93m";
    constexpr auto BB   = "\033[94m";
    constexpr auto BM   = "\033[95m";
    constexpr auto BC   = "\033[96m";
    constexpr auto BW   = "\033[97m";

    constexpr auto G  = "\033[32m";
    constexpr auto R_ = "\033[31m";
    constexpr auto C  = "\033[36m";
    constexpr auto Y  = "\033[33m";
    constexpr auto M  = "\033[35m";
    constexpr auto B_ = "\033[34m";

    constexpr auto G7 = "\033[90m";
}

std::string Repeat(const char* utf8, int n);

template <typename N>
std::string Comma(N n) {
    auto s = std::to_string(n);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

std::string Elapsed(std::chrono::steady_clock::time_point start);
std::string CompactQty(Quantity qty);
const char* OrderTypeLabel(OrderType type);
int VisibleLen(const std::string& s);

class Terminal {
public:
    Terminal();
    ~Terminal();
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    int GetKeyPress(int timeoutMs);
    int Width() const;

private:
    struct termios orig_;
};
