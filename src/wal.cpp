#include "orderbook/wal.h"
#include "orderbook/matching_engine.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

[[nodiscard]] static std::string ExpiryToString(TimePoint tp) {
    if (tp == TimePoint::max()) return "max";
    return std::to_string(std::chrono::system_clock::to_time_t(tp));
}

[[nodiscard]] static TimePoint StringToExpiry(const std::string& s) {
    if (s == "max") return TimePoint::max();
    return std::chrono::system_clock::from_time_t(
        static_cast<std::time_t>(std::atoll(s.c_str())));
}

[[nodiscard]] static int OpenForAppend(const char* path) {
    int fd = ::open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return -1;
    if (::lseek(fd, 0, SEEK_END) < 0) { ::close(fd); return -1; }
    return fd;
}

[[nodiscard]] static int OpenForRead(const char* path) {
    return ::open(path, O_RDONLY | O_CLOEXEC);
}

[[nodiscard]] static bool ReadLine(int fd, std::string& out) {
    out.clear();
    char buf;
    while (true) {
        auto n = ::read(fd, &buf, 1);
        if (n <= 0) return !out.empty();
        if (buf == '\n') return true;
        out += buf;
    }
}

Wal::Wal(std::string filepath)
    : filepath_{std::move(filepath)}
    , fd_{-1}
    , healthy_{false}
{
    fd_ = OpenForAppend(filepath_.c_str());
    if (fd_ < 0) return;
    healthy_ = true;

    off_t pos = ::lseek(fd_, 0, SEEK_CUR);
    if (pos == 0) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        auto header = std::string{"#WAL1 "} + std::to_string(now) + '\n';
        WriteLine(header);
    }
}

Wal::~Wal() {
    if (fd_ >= 0) {
        Flush();
        ::close(fd_);
    }
}

Wal::Wal(Wal&& other) noexcept
    : filepath_{std::move(other.filepath_)}
    , fd_{other.fd_}
    , healthy_{other.healthy_}
{
    other.fd_ = -1;
    other.healthy_ = false;
}

Wal& Wal::operator=(Wal&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) { Flush(); ::close(fd_); }
        filepath_ = std::move(other.filepath_);
        fd_ = other.fd_;
        healthy_ = other.healthy_;
        other.fd_ = -1;
        other.healthy_ = false;
    }
    return *this;
}

bool Wal::IsHealthy() const { return healthy_; }

void Wal::Flush() {
    if (fd_ >= 0) ::fsync(fd_);
}

void Wal::WriteLine(const std::string& line) {
    if (!healthy_) return;
    const char* data = line.data();
    std::size_t remaining = line.size();
    while (remaining > 0) {
        auto n = ::write(fd_, data, remaining);
        if (n <= 0) { healthy_ = false; return; }
        data += n;
        remaining -= static_cast<std::size_t>(n);
    }
}

void Wal::WriteAddOrder(const StockId stockId, const Order& order) {
    auto line = std::to_string(stockId) + '|'
              + std::to_string(static_cast<int>(order.GetType())) + '|'
              + std::to_string(static_cast<int>(order.GetTimeInForce())) + '|'
              + std::to_string(order.GetOrderId()) + '|'
              + std::to_string(static_cast<int>(order.GetSide())) + '|'
              + std::to_string(order.GetPrice()) + '|'
              + std::to_string(order.GetInitialQuantity()) + '|'
              + std::to_string(order.GetStopPrice()) + '|'
              + std::to_string(order.GetPeakSize()) + '|'
              + ExpiryToString(order.GetExpiry()) + '\n';
    line.insert(0, "A|");
    WriteLine(line);
}

void Wal::WriteCancelOrder(const StockId stockId, const OrderId orderId) {
    auto line = std::string{"C|"}
              + std::to_string(stockId) + '|'
              + std::to_string(orderId) + '\n';
    WriteLine(line);
}

void Wal::WriteModifyOrder(const StockId stockId, const OrderModify& modify) {
    auto line = std::string{"M|"}
              + std::to_string(stockId) + '|'
              + std::to_string(modify.GetOrderId()) + '|'
              + std::to_string(static_cast<int>(modify.GetSide())) + '|'
              + std::to_string(modify.GetPrice()) + '|'
              + std::to_string(modify.GetQuantity()) + '\n';
    WriteLine(line);
}

std::size_t Wal::Replay(const std::string& filepath, MatchingEngine& engine,
                         OrderId* const maxOrderId) {
    int fd = OpenForRead(filepath.c_str());
    if (fd < 0) return 0;

    std::string line;
    if (!ReadLine(fd, line)) { ::close(fd); return 0; }
    if (line.size() < 6 || line.substr(0, 5) != "#WAL1") { ::close(fd); return 0; }

    std::size_t count = 0;
    OrderId maxId = 0;

    while (ReadLine(fd, line)) {
        if (line.empty()) continue;

        char type = line[0];

        auto first = line.find('|');
        if (first == std::string::npos) continue;

        std::string rest = line.substr(first + 1);

        switch (type) {
        case 'A': {

            std::vector<std::string> fields;
            std::size_t pos = 0;
            while ((pos = rest.find('|')) != std::string::npos) {
                fields.push_back(rest.substr(0, pos));
                rest.erase(0, pos + 1);
            }
            fields.push_back(rest);
            if (fields.size() < 10) continue;

            StockId stockId = static_cast<StockId>(std::atol(fields[0].c_str()));
            auto type_ = static_cast<OrderType>(std::atoi(fields[1].c_str()));
            auto tif = static_cast<TimeInForce>(std::atoi(fields[2].c_str()));
            OrderId orderId = static_cast<OrderId>(std::atoll(fields[3].c_str()));
            auto side = static_cast<Side>(std::atoi(fields[4].c_str()));
            Price price = static_cast<Price>(std::atol(fields[5].c_str()));
            Quantity qty = static_cast<Quantity>(std::atol(fields[6].c_str()));
            Price stopPrice = static_cast<Price>(std::atol(fields[7].c_str()));
            Quantity peakSize = static_cast<Quantity>(std::atol(fields[8].c_str()));
            TimePoint expiry = StringToExpiry(fields[9]);

            auto order = std::make_shared<Order>(
                type_, tif, orderId, side, price, qty, stopPrice, peakSize, expiry);
            engine.PlaceOrder(stockId, std::move(order));
            if (orderId > maxId) maxId = orderId;
            ++count;
            break;
        }
        case 'C': {

            auto sep = rest.find('|');
            if (sep == std::string::npos) continue;
            StockId stockId = static_cast<StockId>(std::atol(rest.substr(0, sep).c_str()));
            OrderId orderId = static_cast<OrderId>(std::atoll(rest.substr(sep + 1).c_str()));
            engine.CancelOrder(stockId, orderId);
            if (orderId > maxId) maxId = orderId;
            ++count;
            break;
        }
        case 'M': {

            std::vector<std::string> fields;
            std::size_t pos = 0;
            while ((pos = rest.find('|')) != std::string::npos) {
                fields.push_back(rest.substr(0, pos));
                rest.erase(0, pos + 1);
            }
            fields.push_back(rest);
            if (fields.size() < 5) continue;

            StockId stockId = static_cast<StockId>(std::atol(fields[0].c_str()));
            OrderId orderId = static_cast<OrderId>(std::atoll(fields[1].c_str()));
            auto side = static_cast<Side>(std::atoi(fields[2].c_str()));
            Price price = static_cast<Price>(std::atol(fields[3].c_str()));
            Quantity qty = static_cast<Quantity>(std::atol(fields[4].c_str()));

            engine.ModifyOrder(stockId, OrderModify(orderId, side, price, qty));
            if (orderId > maxId) maxId = orderId;
            ++count;
            break;
        }
        }
    }

    ::close(fd);
    if (maxOrderId) *maxOrderId = maxId;
    return count;
}
