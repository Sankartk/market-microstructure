#pragma once

#include "order.hpp"
#include <unordered_map>
#include <map>
#include <vector>
#include <functional>
#include <optional>

namespace mm {

// Lock-free single-producer single-consumer order book.
// Bids sorted descending (best bid = begin()), asks ascending.
class OrderBook {
public:
    using TradeCallback = std::function<void(OrderId aggressive_id, OrderId passive_id,
                                              Price price, Qty qty, TsNs ts)>;

    explicit OrderBook(size_t initial_capacity = 1 << 20);

    // Non-copyable — order book is a unique resource
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    bool add_order(OrderId id, Price price, Qty qty, Side side, TsNs ts);
    bool cancel_order(OrderId id);
    bool execute_order(OrderId id, Qty exec_qty, TsNs ts);
    bool replace_order(OrderId id, Price new_price, Qty new_qty, TsNs ts);

    [[nodiscard]] BookSnapshot snapshot(TsNs ts) const;
    [[nodiscard]] const Order* get_order(OrderId id) const;
    [[nodiscard]] size_t total_orders() const { return order_map_.size(); }
    [[nodiscard]] size_t bid_levels() const { return bids_.size(); }
    [[nodiscard]] size_t ask_levels() const { return asks_.size(); }

    // Returns top N levels for a given side, sorted best-first
    [[nodiscard]] std::vector<PriceLevel> top_levels(Side side, size_t n) const;

    void set_trade_callback(TradeCallback cb) { on_trade_ = std::move(cb); }

private:
    // Flat pool allocator — avoids malloc/free in hot path
    struct alignas(64) PoolSlot {
        Order order;
        bool  in_use;
    };

    std::vector<PoolSlot> pool_;
    std::vector<size_t>   free_list_;

    std::unordered_map<OrderId, size_t> order_map_;  // id → pool index

    // Bids: highest first. Asks: lowest first.
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel, std::less<Price>>    asks_;

    TradeCallback on_trade_;

    size_t allocate_slot();
    void   release_slot(size_t idx);

    void add_to_level(Side side, Price price, OrderId pool_idx);
    void remove_from_level(Side side, Price price, OrderId pool_idx);
    void update_level_qty(Side side, Price price, Qty delta, bool is_add);

    PriceLevel* find_level(Side side, Price price);
    const PriceLevel* find_level(Side side, Price price) const;
};

} // namespace mm
