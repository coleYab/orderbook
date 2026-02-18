#pragma once

#include "order.h"
#include "types.h"

#include <cstddef>
#include <cstdint>
#include <string>

class MatchingEngine;
class OrderModify;

class Wal {
public:
    explicit Wal(std::string filepath);
    ~Wal();

    Wal(const Wal&) = delete;
    Wal& operator=(const Wal&) = delete;

    Wal(Wal&& other) noexcept;
    Wal& operator=(Wal&& other) noexcept;

    void WriteAddOrder(StockId stockId, const Order& order);
    void WriteCancelOrder(StockId stockId, OrderId orderId);
    void WriteModifyOrder(StockId stockId, const OrderModify& modify);

    void Flush();

    [[nodiscard]] bool IsHealthy() const;

    static std::size_t Replay(const std::string& filepath, MatchingEngine& engine,
                              OrderId* maxOrderId = nullptr);

private:
    void WriteLine(const std::string& line);

    std::string filepath_;
    int fd_;
    bool healthy_;
};
