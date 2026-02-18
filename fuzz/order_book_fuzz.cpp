#include "orderbook/order_book.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifndef STANDALONE_FUZZ

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
#else

static int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
#endif

    if (size < 9) return 0;

    OrderBook book;
    OrderId nextId = 1;

    size_t offset = 0;
    while (offset + 9 <= size) {
        const uint8_t opcode = data[offset] % 4;
        ++offset;

        const uint8_t side_byte = data[offset] % 2;
        const auto side = (side_byte == 0) ? Side::Buy : Side::Sell;

        int32_t price;
        uint32_t quantity;
        std::memcpy(&price, data + offset + 1, 4);
        std::memcpy(&quantity, data + offset + 5, 4);

        price = std::max(Price{1}, std::min(Price{10000}, price));
        quantity = std::max(Quantity{1}, std::min(Quantity{10000}, quantity));

        switch (opcode) {
        case 0:
            book.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, nextId++, side, price, quantity));
            break;
        case 1:
            book.AddOrder(std::make_shared<Order>(OrderType::FillAndKill, nextId++, side, price, quantity));
            break;
        case 2: {
            int64_t id;
            std::memcpy(&id, data + offset, 8);
            id = std::max(OrderId{0}, id % nextId);
            book.CancelOrder(id);
            break;
        }
        case 3:
            book.ModifyOrder(OrderModify{nextId++, side, price, quantity});
            break;
        }

        offset += 8;
    }

    const auto infos = book.GetOrderLevelInfos();
    const auto bestBid = infos.GetBids().empty() ? Price{0} : infos.GetBids().front().price_;
    const auto bestAsk = infos.GetAsks().empty() ? Price{0} : infos.GetAsks().front().price_;

    if (!infos.GetBids().empty() && !infos.GetAsks().empty()) {
        if (bestBid >= bestAsk) __builtin_trap();
    }

    return 0;
}

#ifdef STANDALONE_FUZZ
int main() {

    std::srand(42);
    std::vector<uint8_t> buf(4096);
    for (auto& b : buf) b = static_cast<uint8_t>(std::rand());
    LLVMFuzzerTestOneInput(buf.data(), buf.size());
    return 0;
}
#endif
