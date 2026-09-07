#pragma once

#include "order.hpp"
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace mm {

// NASDAQ ITCH 5.0 binary message parser.
// Parses raw bytes into typed events without heap allocation.
struct ItchMessage {
    MsgType type;
    OrderId order_id;
    Price   price;
    Qty     qty;
    Side    side;
    TsNs    timestamp_ns;
    char    symbol[9];  // null-terminated, up to 8 chars
};

class ItchParser {
public:
    // Parse a single message from buffer. Returns bytes consumed (0 if incomplete).
    [[nodiscard]] size_t parse(const uint8_t* buf, size_t len, ItchMessage& out) const;

    // Parse all complete messages from a buffer. Returns total bytes consumed.
    size_t parse_all(const uint8_t* buf, size_t len,
                     std::vector<ItchMessage>& out) const;

private:
    static constexpr size_t HEADER_SIZE = 2;  // 2-byte big-endian length prefix

    [[nodiscard]] static uint16_t read_u16(const uint8_t* p) {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }
    [[nodiscard]] static uint32_t read_u32(const uint8_t* p) {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) <<  8) |
                static_cast<uint32_t>(p[3]);
    }
    [[nodiscard]] static uint64_t read_u64(const uint8_t* p) {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
        return v;
    }
    [[nodiscard]] static uint64_t read_u48(const uint8_t* p) {
        uint64_t v = 0;
        for (int i = 0; i < 6; ++i) v = (v << 8) | p[i];
        return v;
    }
    [[nodiscard]] static Price itch_price(uint32_t raw) {
        return static_cast<Price>(raw);  // ITCH prices are already in 1/10000
    }
};

} // namespace mm
