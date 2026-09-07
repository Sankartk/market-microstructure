# market-microstructure

A high-performance C++20 order book engine with real-time market abuse pattern detection.

Parses NASDAQ ITCH 5.0 binary feeds, maintains a lock-free order book with nanosecond timestamps, and detects spoofing, layering, momentum ignition, and quote stuffing — all in-process with zero heap allocation in the hot path.

---

## What it demonstrates

| Signal | Where |
|---|---|
| C++20, cache-line aligned data structures | `include/order.hpp`, `src/order_book.cpp` |
| Pool allocator (no malloc in hot path) | `src/order_book.cpp` |
| Binary protocol parsing (ITCH 5.0) | `src/itch_parser.cpp` |
| Real-time pattern detection | `src/detector.cpp` |
| Nanosecond-precision benchmarking | `bench/benchmark.cpp` |

---

## Architecture

```
ITCH 5.0 binary feed
        │
        ▼
┌─────────────────┐
│  ItchParser     │  zero-copy binary parser, no heap alloc
│  (itch_parser)  │
└────────┬────────┘
         │  vector<ItchMessage>
         ▼
┌─────────────────┐
│  FeedHandler    │  routes to per-symbol book, feeds detector
│  (feed_handler) │
└────────┬────────┘
         │
    ┌────┴────┐
    ▼         ▼
┌────────┐ ┌──────────────┐
│OrderBook│ │PatternDetector│  sliding-window abuse detection
│(per sym)│ │(4 patterns)   │
└─────────┘ └──────────────┘
```

---

## Pattern detection

| Pattern | Trigger | Confidence |
|---|---|---|
| **Spoofing** | Large order cancelled within 500ms, no fill, size ≥ 5× book depth | 0.75–1.00 |
| **Layering** | 4+ open orders same side within 5 ticks | 0.70 |
| **Momentum ignition** | 5+ same-direction fills in 2s window | 0.65 |
| **Quote stuffing** | ≥10,000 messages/second per symbol | 0.90 |

---

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Run

```bash
# Process an ITCH file
./build/microstructure data/sample.itch --stats --alerts

# Show top-of-book for a symbol after processing
./build/microstructure data/sample.itch --book AAPL

# Run benchmarks
./build/benchmark
```

## Benchmarks (Release build, single thread)

Measured on Windows 11, GCC 14.2, `-O3 -march=native`, 1M operations:

```
add_order    : ~673 ns/op   (1.5M ops/sec)
cancel_order : ~29  ns/op   (34M ops/sec)
mixed        : ~603 ns/op   (1.7M ops/sec)  50% add / 30% cancel / 20% execute
snapshot     : ~1   ns/op   (1B snapshots/sec)
```

`add_order` is dominated by `std::unordered_map` hash + insert. `cancel_order` is faster because the map lookup hits a warm cache line. Snapshot is O(1) — it reads the root of two `std::map` trees, which are already sorted.

---

## Design decisions

**Pool allocator** — `OrderBook` pre-allocates a flat array of `PoolSlot` structs. `add_order` pulls from a free list, `cancel_order` returns to it. Zero `malloc`/`free` calls after warmup. Pool doubles automatically if exhausted.

**Intrusive price levels** — each `PriceLevel` tracks head/tail order IDs for an intrusive doubly-linked list within the level. FIFO order within a level is preserved without a separate container.

**Cache-line alignment** — `Order` is `alignas(64)`. In a multi-threaded extension, two threads operating on adjacent orders will not cause false sharing.

**Fixed-point prices** — prices are `int64_t` in units of 1/10000 of a dollar. No floating-point comparison errors in price level lookups.

**Sliding window detection** — `PatternDetector` keeps a 60-second rolling window of order events per symbol. All detection algorithms run in O(window) with early exit, not O(history).
