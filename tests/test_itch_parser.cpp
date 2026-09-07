#include "itch_parser.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

using namespace mm;

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                  << "  " << #cond << "\n"; \
        ++failures; \
    } } while (0)

// Build a raw ITCH Add Order message (type 'A', 36 bytes body + 2-byte header)
// Body layout (body-relative, p = buf+2):
// type(1) stock_locate(2) tracking(2) timestamp(6) order_id(8)
// side(1) qty(4) symbol(8) price(4)
static std::vector<uint8_t> make_add_order(
    uint64_t order_id, uint32_t qty, uint32_t price, char side,
    const char* symbol, uint64_t ts_ns = 0)
{
    std::vector<uint8_t> buf(40, 0);  // 2 header + 38 max body
    // length prefix
    buf[0] = 0; buf[1] = 36;
    // type
    buf[2] = 'A';
    // body starts at buf+2; field offsets are body-relative
    uint8_t* p = buf.data() + 2;
    // stock_locate(2) + tracking(2) = p[1..4], zeroed
    // timestamp: 6 bytes big-endian at p[5..10]
    for (int i = 0; i < 6; ++i)
        p[5 + i] = static_cast<uint8_t>((ts_ns >> (8 * (5 - i))) & 0xFF);
    // order id: 8 bytes big-endian at p[13..20]  (wait: p[11..18] is ts end+2)
    // Actually: ts at p[5..10], order_id at p[11..18]
    for (int i = 0; i < 8; ++i)
        p[11 + i] = static_cast<uint8_t>((order_id >> (8 * (7 - i))) & 0xFF);
    // side at p[19]
    p[19] = static_cast<uint8_t>(side);
    // qty: 4 bytes big-endian at p[20..23]
    for (int i = 0; i < 4; ++i)
        p[20 + i] = static_cast<uint8_t>((qty >> (8 * (3 - i))) & 0xFF);
    // symbol: 8 bytes space-padded at p[24..31]
    std::memset(p + 24, ' ', 8);
    std::memcpy(p + 24, symbol, std::strlen(symbol));
    // price: 4 bytes big-endian at p[32..35]
    for (int i = 0; i < 4; ++i)
        p[32 + i] = static_cast<uint8_t>((price >> (8 * (3 - i))) & 0xFF);
    return buf;
}

static void test_parse_add_order() {
    auto buf = make_add_order(42, 500, 1000000, 'B', "AAPL");
    ItchParser parser;
    ItchMessage msg;
    size_t n = parser.parse(buf.data(), buf.size(), msg);

    CHECK(n == 38);
    CHECK(msg.type == MsgType::Add);
    CHECK(msg.order_id == 42);
    CHECK(msg.qty == 500);
    CHECK(msg.price == 1000000);
    CHECK(msg.side == Side::Buy);
    CHECK(std::strcmp(msg.symbol, "AAPL") == 0);
}

static void test_parse_add_sell_side() {
    auto buf = make_add_order(7, 100, 999000, 'S', "TSLA");
    ItchParser parser;
    ItchMessage msg;
    size_t n = parser.parse(buf.data(), buf.size(), msg);
    CHECK(n == 38);
    CHECK(msg.side == Side::Sell);
}

static void test_incomplete_buffer_returns_zero() {
    auto buf = make_add_order(1, 100, 1000000, 'B', "X");
    ItchParser parser;
    ItchMessage msg;
    // Truncate to 10 bytes — parser must not crash and must return 0
    CHECK(parser.parse(buf.data(), 10, msg) == 0);
}

static void test_symbol_trailing_spaces_trimmed() {
    auto buf = make_add_order(1, 100, 1000000, 'B', "AB");
    ItchParser parser;
    ItchMessage msg;
    size_t n = parser.parse(buf.data(), buf.size(), msg);
    CHECK(n == 38);
    CHECK(std::strcmp(msg.symbol, "AB") == 0);
}

static void test_parse_all_multiple_messages() {
    auto b1 = make_add_order(1, 100, 1000000, 'B', "AAPL");
    auto b2 = make_add_order(2, 200, 1001000, 'S', "AAPL");
    // trim trailing zero-padding — each message is exactly 2+36=38 bytes
    b1.resize(38);
    b2.resize(38);

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), b1.begin(), b1.end());
    combined.insert(combined.end(), b2.begin(), b2.end());

    ItchParser parser;
    std::vector<ItchMessage> out;
    size_t consumed = parser.parse_all(combined.data(), combined.size(), out);

    CHECK(consumed == combined.size());
    CHECK(out.size() == 2);
    CHECK(out[0].order_id == 1);
    CHECK(out[1].order_id == 2);
}

int main() {
    test_parse_add_order();
    test_parse_add_sell_side();
    test_incomplete_buffer_returns_zero();
    test_symbol_trailing_spaces_trimmed();
    test_parse_all_multiple_messages();

    if (failures == 0) {
        std::cout << "[test_itch_parser] all tests passed\n";
        return 0;
    }
    std::cerr << "[test_itch_parser] " << failures << " test(s) failed\n";
    return 1;
}
