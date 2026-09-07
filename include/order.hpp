#pragma once

#include <cstdint>
#include <cstring>
#include <compare>

namespace mm {

using Price   = int64_t;   // fixed-point: price * 10000 (4 decimal places)
using Qty     = uint32_t;
using OrderId = uint64_t;
using TsNs    = uint64_t;  // nanoseconds since midnight

enum class Side : uint8_t { Buy = 0, Sell = 1 };
enum class MsgType : uint8_t {
    Add = 'A', Cancel = 'X', Execute = 'E', Replace = 'R', Delete = 'D'
};

struct alignas(64) Order {  // cache-line aligned to prevent false sharing
    OrderId id;
    Price   price;
    Qty     qty;
    Side    side;
    TsNs    timestamp;
    OrderId prev_id;  // intrusive linked list within price level
    OrderId next_id;

    Order() = default;
    Order(OrderId id_, Price p, Qty q, Side s, TsNs ts)
        : id(id_), price(p), qty(q), side(s), timestamp(ts),
          prev_id(0), next_id(0) {}
};

struct PriceLevel {
    Price   price;
    Qty     total_qty;
    uint32_t order_count;
    OrderId head_id;
    OrderId tail_id;

    PriceLevel() : price(0), total_qty(0), order_count(0), head_id(0), tail_id(0) {}
    explicit PriceLevel(Price p) : price(p), total_qty(0), order_count(0), head_id(0), tail_id(0) {}
};

struct BookSnapshot {
    Price best_bid;
    Price best_ask;
    Qty   bid_depth;
    Qty   ask_depth;
    TsNs  timestamp;
    double mid_price() const {
        if (best_bid == 0 || best_ask == 0) return 0.0;
        return static_cast<double>(best_bid + best_ask) / 2.0 / 10000.0;
    }
    double spread_bps() const {
        if (best_bid == 0 || best_ask == 0) return 0.0;
        return static_cast<double>(best_ask - best_bid) / best_bid * 10000.0;
    }
};

} // namespace mm
