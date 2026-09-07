#include "order_book.hpp"
#include "itch_parser.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <vector>

using namespace mm;
using Clock = std::chrono::high_resolution_clock;

static double elapsed_ns(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration<double, std::nano>(t1 - t0).count();
}

int main() {
    constexpr size_t N = 1'000'000;
    std::mt19937_64 rng(42);

    // ── Benchmark 1: add_order throughput ─────────────────────────────────
    {
        OrderBook book(1 << 20);
        std::uniform_int_distribution<Price> price_dist(900000, 1100000);
        std::uniform_int_distribution<Qty>   qty_dist(100, 10000);
        std::uniform_int_distribution<int>   side_dist(0, 1);

        auto t0 = Clock::now();
        for (size_t i = 0; i < N; ++i) {
            book.add_order(i + 1,
                           price_dist(rng),
                           qty_dist(rng),
                           side_dist(rng) ? Side::Buy : Side::Sell,
                           i);
        }
        auto t1 = Clock::now();
        double ns_per_op = elapsed_ns(t0, t1) / N;
        std::cout << "[bench] add_order    : " << std::fixed << std::setprecision(1)
                  << ns_per_op << " ns/op  (" << (1e9 / ns_per_op / 1e6)
                  << "M ops/sec)\n";
    }

    // ── Benchmark 2: cancel_order throughput ──────────────────────────────
    {
        OrderBook book(1 << 20);
        for (size_t i = 0; i < N; ++i)
            book.add_order(i + 1, 1000000, 1000, Side::Buy, i);

        auto t0 = Clock::now();
        for (size_t i = 0; i < N; ++i)
            book.cancel_order(i + 1);
        auto t1 = Clock::now();
        double ns_per_op = elapsed_ns(t0, t1) / N;
        std::cout << "[bench] cancel_order : " << std::fixed << std::setprecision(1)
                  << ns_per_op << " ns/op  (" << (1e9 / ns_per_op / 1e6)
                  << "M ops/sec)\n";
    }

    // ── Benchmark 3: mixed workload ────────────────────────────────────────
    {
        OrderBook book(1 << 20);
        std::uniform_int_distribution<Price> price_dist(900000, 1100000);
        std::uniform_int_distribution<Qty>   qty_dist(100, 5000);
        std::uniform_int_distribution<int>   op_dist(0, 9);
        std::uniform_int_distribution<int>   side_dist(0, 1);

        std::vector<OrderId> live_ids;
        live_ids.reserve(N / 2);
        OrderId next_id = 1;

        auto t0 = Clock::now();
        for (size_t i = 0; i < N; ++i) {
            int op = op_dist(rng);
            if (op < 5 || live_ids.empty()) {
                book.add_order(next_id, price_dist(rng), qty_dist(rng),
                               side_dist(rng) ? Side::Buy : Side::Sell, i);
                live_ids.push_back(next_id++);
            } else if (op < 8) {
                std::uniform_int_distribution<size_t> idx_dist(0, live_ids.size() - 1);
                size_t idx = idx_dist(rng);
                book.cancel_order(live_ids[idx]);
                live_ids.erase(live_ids.begin() + idx);
            } else {
                std::uniform_int_distribution<size_t> idx_dist(0, live_ids.size() - 1);
                book.execute_order(live_ids[idx_dist(rng)], 100, i);
            }
        }
        auto t1 = Clock::now();
        double ns_per_op = elapsed_ns(t0, t1) / N;
        std::cout << "[bench] mixed (50/30/20 add/cancel/exec): "
                  << std::fixed << std::setprecision(1) << ns_per_op
                  << " ns/op  (" << (1e9 / ns_per_op / 1e6) << "M ops/sec)\n";
    }

    // ── Benchmark 4: snapshot latency ─────────────────────────────────────
    {
        OrderBook book(1 << 20);
        std::uniform_int_distribution<Price> price_dist(900000, 1100000);
        for (size_t i = 0; i < 100000; ++i)
            book.add_order(i + 1, price_dist(rng), 1000, Side::Buy, i);

        auto t0 = Clock::now();
        constexpr int SNAPS = 10000;
        for (int i = 0; i < SNAPS; ++i)
            (void)book.snapshot(i);
        auto t1 = Clock::now();
        double ns_per_op = elapsed_ns(t0, t1) / SNAPS;
        std::cout << "[bench] snapshot     : " << std::fixed << std::setprecision(1)
                  << ns_per_op << " ns/op  (" << (1e9 / ns_per_op / 1e3)
                  << "K snapshots/sec)\n";
    }

    std::cout << "\n[bench] done\n";
    return 0;
}
