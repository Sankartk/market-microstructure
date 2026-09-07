#include "order_book.hpp"
#include <cassert>
#include <iostream>

using namespace mm;

#define CHECK(cond) \
    do { if (!(cond)) { \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                  << "  " << #cond << "\n"; \
        ++failures; \
    } } while (0)

static int failures = 0;

// ── add_order ────────────────────────────────────────────────────────────────

static void test_add_order_basic() {
    OrderBook book;
    CHECK(book.add_order(1, 1000000, 500, Side::Buy, 0));
    CHECK(book.total_orders() == 1);

    const Order* o = book.get_order(1);
    CHECK(o != nullptr);
    CHECK(o->price == 1000000);
    CHECK(o->qty   == 500);
    CHECK(o->side  == Side::Buy);
}

static void test_add_order_duplicate_id_rejected() {
    OrderBook book;
    CHECK(book.add_order(1, 1000000, 500, Side::Buy, 0));
    CHECK(!book.add_order(1, 2000000, 300, Side::Sell, 1));
    CHECK(book.total_orders() == 1);
}

static void test_add_order_zero_qty_rejected() {
    OrderBook book;
    CHECK(!book.add_order(1, 1000000, 0, Side::Buy, 0));
    CHECK(book.total_orders() == 0);
}

// ── cancel_order ─────────────────────────────────────────────────────────────

static void test_cancel_order() {
    OrderBook book;
    book.add_order(1, 1000000, 500, Side::Buy, 0);
    CHECK(book.cancel_order(1));
    CHECK(book.total_orders() == 0);
    CHECK(book.get_order(1) == nullptr);
}

static void test_cancel_order_not_found() {
    OrderBook book;
    CHECK(!book.cancel_order(999));
}

// ── execute_order ────────────────────────────────────────────────────────────

static void test_execute_partial_fill() {
    OrderBook book;
    book.add_order(1, 1000000, 1000, Side::Sell, 0);
    CHECK(book.execute_order(1, 400, 0));
    const Order* o = book.get_order(1);
    CHECK(o != nullptr);
    CHECK(o->qty == 600);
}

static void test_execute_full_fill_removes_order() {
    OrderBook book;
    book.add_order(1, 1000000, 500, Side::Sell, 0);
    CHECK(book.execute_order(1, 500, 0));
    CHECK(book.get_order(1) == nullptr);
    CHECK(book.total_orders() == 0);
}

static void test_execute_overfill_clamps_to_available() {
    OrderBook book;
    book.add_order(1, 1000000, 200, Side::Buy, 0);
    CHECK(book.execute_order(1, 9999, 0));
    CHECK(book.get_order(1) == nullptr);
}

// ── replace_order ────────────────────────────────────────────────────────────

static void test_replace_order() {
    OrderBook book;
    book.add_order(1, 1000000, 500, Side::Buy, 0);
    CHECK(book.replace_order(1, 2000000, 800, 1));
    const Order* o = book.get_order(1);
    CHECK(o != nullptr);
    CHECK(o->price == 2000000);
    CHECK(o->qty   == 800);
}

// ── snapshot ─────────────────────────────────────────────────────────────────

static void test_best_bid_ask() {
    OrderBook book;
    book.add_order(1, 1000000, 100, Side::Buy,  0);  // bid 100.00
    book.add_order(2, 1001000, 100, Side::Buy,  1);  // bid 100.10
    book.add_order(3, 1002000, 200, Side::Sell, 2);  // ask 100.20
    book.add_order(4, 1003000, 150, Side::Sell, 3);  // ask 100.30

    auto snap = book.snapshot(4);
    CHECK(snap.best_bid == 1001000);
    CHECK(snap.best_ask == 1002000);
    CHECK(snap.spread_bps() > 0.0);
}

static void test_empty_book_snapshot_is_zero() {
    OrderBook book;
    auto snap = book.snapshot(0);
    CHECK(snap.best_bid == 0);
    CHECK(snap.best_ask == 0);
    CHECK(snap.mid_price() == 0.0);
}

// ── top_levels ───────────────────────────────────────────────────────────────

static void test_top_levels_bids_descending() {
    OrderBook book;
    book.add_order(1, 1000000, 100, Side::Buy, 0);
    book.add_order(2,  999000, 200, Side::Buy, 1);
    book.add_order(3,  998000, 150, Side::Buy, 2);

    auto levels = book.top_levels(Side::Buy, 3);
    CHECK(levels.size() == 3);
    CHECK(levels[0].price == 1000000);
    CHECK(levels[1].price ==  999000);
    CHECK(levels[2].price ==  998000);
}

static void test_top_levels_asks_ascending() {
    OrderBook book;
    book.add_order(1, 1001000, 100, Side::Sell, 0);
    book.add_order(2, 1002000, 200, Side::Sell, 1);

    auto levels = book.top_levels(Side::Sell, 2);
    CHECK(levels.size() == 2);
    CHECK(levels[0].price == 1001000);
    CHECK(levels[1].price == 1002000);
}

// ── pool growth ──────────────────────────────────────────────────────────────

static void test_pool_grows_automatically() {
    OrderBook book(4);  // tiny initial pool
    for (OrderId i = 1; i <= 100; ++i)
        CHECK(book.add_order(i, 1000000 + static_cast<Price>(i), 100, Side::Buy, i));
    CHECK(book.total_orders() == 100);
}

int main() {
    test_add_order_basic();
    test_add_order_duplicate_id_rejected();
    test_add_order_zero_qty_rejected();
    test_cancel_order();
    test_cancel_order_not_found();
    test_execute_partial_fill();
    test_execute_full_fill_removes_order();
    test_execute_overfill_clamps_to_available();
    test_replace_order();
    test_best_bid_ask();
    test_empty_book_snapshot_is_zero();
    test_top_levels_bids_descending();
    test_top_levels_asks_ascending();
    test_pool_grows_automatically();

    if (failures == 0) {
        std::cout << "[test_order_book] all tests passed\n";
        return 0;
    }
    std::cerr << "[test_order_book] " << failures << " test(s) failed\n";
    return 1;
}
