#include "itch_parser.hpp"
#include <cstring>

namespace mm {

size_t ItchParser::parse(const uint8_t* buf, size_t len, ItchMessage& out) const {
    if (len < HEADER_SIZE) return 0;

    uint16_t msg_len = read_u16(buf);
    if (len < HEADER_SIZE + msg_len) return 0;

    const uint8_t* p = buf + HEADER_SIZE;
    char type = static_cast<char>(p[0]);
    std::memset(&out, 0, sizeof(out));

    switch (type) {
        case 'A': {  // Add Order (36 bytes)
            if (msg_len < 36) return 0;
            out.type      = MsgType::Add;
            // skip stock locate (2) + tracking number (2)
            out.timestamp_ns = read_u48(p + 5);
            out.order_id     = read_u64(p + 11);
            out.side         = (p[19] == 'B') ? Side::Buy : Side::Sell;
            out.qty          = read_u32(p + 20);
            std::memcpy(out.symbol, p + 24, 8);
            out.symbol[8] = '\0';
            // trim trailing spaces from symbol
            for (int i = 7; i >= 0 && out.symbol[i] == ' '; --i) out.symbol[i] = '\0';
            out.price = itch_price(read_u32(p + 32));
            return HEADER_SIZE + 36;
        }
        case 'X': {  // Order Cancel (23 bytes)
            if (msg_len < 23) return 0;
            out.type         = MsgType::Cancel;
            out.timestamp_ns = read_u48(p + 5);
            out.order_id     = read_u64(p + 11);
            out.qty          = read_u32(p + 19);
            return HEADER_SIZE + 23;
        }
        case 'E': {  // Order Executed (31 bytes)
            if (msg_len < 31) return 0;
            out.type         = MsgType::Execute;
            out.timestamp_ns = read_u48(p + 5);
            out.order_id     = read_u64(p + 11);
            out.qty          = read_u32(p + 19);
            return HEADER_SIZE + 31;
        }
        case 'D': {  // Order Delete (19 bytes)
            if (msg_len < 19) return 0;
            out.type         = MsgType::Delete;
            out.timestamp_ns = read_u48(p + 5);
            out.order_id     = read_u64(p + 11);
            return HEADER_SIZE + 19;
        }
        case 'R': {  // Order Replace (35 bytes)
            if (msg_len < 35) return 0;
            out.type         = MsgType::Replace;
            out.timestamp_ns = read_u48(p + 5);
            out.order_id     = read_u64(p + 11);  // original order id
            // new_order_id at p+19 — we track by new id after replace
            out.qty          = read_u32(p + 27);
            out.price        = itch_price(read_u32(p + 31));
            return HEADER_SIZE + 35;
        }
        default:
            return 0;  // unknown type — skip
    }
}

size_t ItchParser::parse_all(const uint8_t* buf, size_t len,
                              std::vector<ItchMessage>& out) const {
    size_t consumed = 0;
    while (consumed < len) {
        ItchMessage msg;
        size_t n = parse(buf + consumed, len - consumed, msg);
        if (n == 0) break;
        if (msg.type != MsgType::Add && msg.type != MsgType::Cancel &&
            msg.type != MsgType::Execute && msg.type != MsgType::Delete &&
            msg.type != MsgType::Replace) {
            consumed += n;
            continue;
        }
        out.push_back(msg);
        consumed += n;
    }
    return consumed;
}

} // namespace mm
