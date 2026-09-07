#include "order_book.hpp"
#include <stdexcept>
#include <algorithm>

namespace mm {

OrderBook::OrderBook(size_t initial_capacity) {
    pool_.resize(initial_capacity);
    free_list_.reserve(initial_capacity);
    order_map_.reserve(initial_capacity * 2);  // avoid rehash in hot path
    for (size_t i = 0; i < initial_capacity; ++i) {
        pool_[i].in_use = false;
        free_list_.push_back(initial_capacity - 1 - i);  // reverse so slot 0 is first
    }
}

size_t OrderBook::allocate_slot() {
    if (free_list_.empty()) {
        // Grow pool by 2x
        size_t old_size = pool_.size();
        pool_.resize(old_size * 2);
        free_list_.reserve(old_size * 2);
        for (size_t i = old_size; i < old_size * 2; ++i) {
            pool_[i].in_use = false;
            free_list_.push_back(i);
        }
    }
    size_t idx = free_list_.back();
    free_list_.pop_back();
    pool_[idx].in_use = true;
    return idx;
}

void OrderBook::release_slot(size_t idx) {
    pool_[idx].in_use = false;
    pool_[idx].order = Order{};
    free_list_.push_back(idx);
}

bool OrderBook::add_order(OrderId id, Price price, Qty qty, Side side, TsNs ts) {
    if (qty == 0 || order_map_.count(id)) return false;

    size_t idx = allocate_slot();
    pool_[idx].order = Order(id, price, qty, side, ts);
    order_map_[id] = idx;

    add_to_level(side, price, id);
    update_level_qty(side, price, qty, true);
    return true;
}

bool OrderBook::cancel_order(OrderId id) {
    auto it = order_map_.find(id);
    if (it == order_map_.end()) return false;

    size_t idx = it->second;
    const Order& o = pool_[idx].order;

    update_level_qty(o.side, o.price, o.qty, false);
    remove_from_level(o.side, o.price, id);
    order_map_.erase(it);
    release_slot(idx);
    return true;
}

bool OrderBook::execute_order(OrderId id, Qty exec_qty, TsNs ts) {
    auto it = order_map_.find(id);
    if (it == order_map_.end()) return false;

    size_t idx = it->second;
    Order& o = pool_[idx].order;
    Qty fill_qty = std::min(exec_qty, o.qty);

    update_level_qty(o.side, o.price, fill_qty, false);

    if (on_trade_) {
        on_trade_(id, id, o.price, fill_qty, ts);
    }

    o.qty -= fill_qty;
    if (o.qty == 0) {
        remove_from_level(o.side, o.price, id);
        order_map_.erase(it);
        release_slot(idx);
    }
    return true;
}

bool OrderBook::replace_order(OrderId id, Price new_price, Qty new_qty, TsNs ts) {
    auto it = order_map_.find(id);
    if (it == order_map_.end()) return false;

    size_t idx = it->second;
    const Order& old = pool_[idx].order;
    Side side = old.side;
    Price old_price = old.price;
    Qty old_qty = old.qty;

    // Remove old
    update_level_qty(side, old_price, old_qty, false);
    remove_from_level(side, old_price, id);

    // Insert new
    pool_[idx].order = Order(id, new_price, new_qty, side, ts);
    add_to_level(side, new_price, id);
    update_level_qty(side, new_price, new_qty, true);
    return true;
}

BookSnapshot OrderBook::snapshot(TsNs ts) const {
    BookSnapshot snap{};
    snap.timestamp = ts;

    if (!bids_.empty()) {
        snap.best_bid  = bids_.begin()->first;
        snap.bid_depth = bids_.begin()->second.total_qty;
    }
    if (!asks_.empty()) {
        snap.best_ask  = asks_.begin()->first;
        snap.ask_depth = asks_.begin()->second.total_qty;
    }
    return snap;
}

const Order* OrderBook::get_order(OrderId id) const {
    auto it = order_map_.find(id);
    return it != order_map_.end() ? &pool_[it->second].order : nullptr;
}

std::vector<PriceLevel> OrderBook::top_levels(Side side, size_t n) const {
    std::vector<PriceLevel> result;
    result.reserve(n);

    if (side == Side::Buy) {
        for (auto it = bids_.begin(); it != bids_.end() && result.size() < n; ++it)
            result.push_back(it->second);
    } else {
        for (auto it = asks_.begin(); it != asks_.end() && result.size() < n; ++it)
            result.push_back(it->second);
    }
    return result;
}

void OrderBook::add_to_level(Side side, Price price, OrderId /* id */) {
    if (side == Side::Buy) {
        auto& lvl = bids_[price];
        if (lvl.order_count == 0) lvl.price = price;
        ++lvl.order_count;
    } else {
        auto& lvl = asks_[price];
        if (lvl.order_count == 0) lvl.price = price;
        ++lvl.order_count;
    }
}

void OrderBook::remove_from_level(Side side, Price price, OrderId /* id */) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end() && --it->second.order_count == 0)
            bids_.erase(it);
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end() && --it->second.order_count == 0)
            asks_.erase(it);
    }
}

void OrderBook::update_level_qty(Side side, Price price, Qty delta, bool is_add) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end()) {
            if (is_add) it->second.total_qty += delta;
            else        it->second.total_qty -= delta;
        }
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) {
            if (is_add) it->second.total_qty += delta;
            else        it->second.total_qty -= delta;
        }
    }
}

PriceLevel* OrderBook::find_level(Side side, Price price) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it != bids_.end() ? &it->second : nullptr;
    }
    auto it = asks_.find(price);
    return it != asks_.end() ? &it->second : nullptr;
}

const PriceLevel* OrderBook::find_level(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        return it != bids_.end() ? &it->second : nullptr;
    }
    auto it = asks_.find(price);
    return it != asks_.end() ? &it->second : nullptr;
}

} // namespace mm
