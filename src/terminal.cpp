#include "orderbook/terminal.h"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <format>

std::string Repeat(const char* utf8, int n) {
    std::string r;
    std::string_view sv(utf8);
    for (int i = 0; i < n; ++i) r += sv;
    return r;
}

std::string Elapsed(std::chrono::steady_clock::time_point start) {
    auto d = std::chrono::steady_clock::now() - start;
    auto h = std::chrono::duration_cast<std::chrono::hours>(d);
    auto m = std::chrono::duration_cast<std::chrono::minutes>(d - h);
    auto s = std::chrono::duration_cast<std::chrono::seconds>(d - h - m);
    return std::format("{:02}:{:02}:{:02}",
        static_cast<int>(h.count()),
        static_cast<int>(m.count()),
        static_cast<int>(s.count()));
}

std::string CompactQty(Quantity qty) {
    if (qty >= 1000000) return std::format("{:.1f}M", static_cast<double>(qty) / 1000000.0);
    if (qty >= 1000)    return std::format("{:.1f}K", static_cast<double>(qty) / 1000.0);
    return std::to_string(qty);
}

const char* OrderTypeLabel(OrderType type) {
    switch (type) {
        case OrderType::Limit:      return "LMT";
        case OrderType::Market:     return "MKT";
        case OrderType::StopLimit:  return "STP";
        case OrderType::StopMarket: return "STP_MKT";
    }
    return "???";
}

int VisibleLen(const std::string& s) {
    int n = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033') { while (i < s.size() && s[i] != 'm') ++i; }
        else { ++n; }
    }
    return n;
}

Terminal::Terminal() {
    termios orig{};
    tcgetattr(STDIN_FILENO, &orig);
    orig_ = orig;
    termios raw = orig;
    raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

Terminal::~Terminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
}

int Terminal::GetKeyPress(int timeoutMs) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
        char c{};
        if (read(STDIN_FILENO, &c, 1) > 0) return static_cast<unsigned char>(c);
    }
    return -1;
}

int Terminal::Width() const {
    winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return static_cast<int>(w.ws_col);
    return 80;
}
