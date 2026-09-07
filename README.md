# market-microstructure

**What happens inside an exchange between the moment you click "buy" and the moment you own the shares?**

This project is my answer to that question: a limit order book written in C++20 that ingests the same binary feed format NASDAQ actually uses (ITCH 5.0), keeps the book updated in nanoseconds, and watches for four types of market manipulation while it works.

---

## Why I built it

Most backend work I do is measured in milliseconds. I wanted to understand the world where a microsecond is slow — where the data structure you pick and the allocator you use decide whether you're competitive.

So I picked the hardest version of the problem I could build alone: reconstruct NASDAQ's order book from raw ITCH messages, then detect spoofing and layering the way surveillance teams at exchanges do.

## What I learned building it

- **My first benchmark numbers were wrong.** The initial version claimed ~45ns per `add_order`. When I actually measured it, the real number was ~673ns — dominated by hash map rehashing and an accidental O(n) erase in my benchmark harness. Fixing the measurement was as instructive as fixing the code. The numbers below are the real, reproducible ones.
- **A "correct" parser isn't correct until it's tested at the byte level.** My first ITCH parser had every field offset off by 2 bytes. It compiled, it ran, it produced plausible-looking garbage. Tests caught it.
- **The compiler is strict about things you'd never guess.** A nested struct with default member initializers can't be used as a default argument on GCC. One-line fix, but it cost me an hour.

## Measured performance

Windows 11, GCC 14.2, `-O2`, 1M operations, single thread:

| Operation | Latency | Throughput |
|---|---|---|
| `add_order` | ~673 ns | 1.5M orders/sec |
| `cancel_order` | ~29 ns | 34M cancels/sec |
| mixed workload | ~603 ns | 1.7M ops/sec |
| book snapshot | ~1 ns | effectively free |

Why is cancel faster than add? Cancels hit an order ID that's already in cache from a recent lookup. Adds pay for a hash map insert plus a tree insertion into a new or existing price level.

## What it detects

| Pattern | What it looks like in the feed | How it's caught |
|---|---|---|
| **Spoofing** | Huge bid appears, never trades, vanishes in <500ms | Track order size vs. book depth; flag fast cancels of oversized orders |
| **Layering** | 4+ orders stacked at adjacent prices, same side | Rolling window over open orders per symbol |
| **Momentum ignition** | Burst of aggressive one-sided fills | Count same-direction fills in a 2s window |
| **Quote stuffing** | 10,000+ messages/sec on one symbol | Message rate per symbol per second |

## Build and run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# run the test suite
ctest --test-dir build --output-on-failure

# process a real ITCH feed file
./build/microstructure data/sample.itch --stats --alerts --book AAPL

# reproduce the benchmarks
./build/benchmark
```

## Design choices worth knowing about

**Pool allocator.** Orders live in a pre-allocated flat array with a free list. After warmup, `add_order` and `cancel_order` never touch `malloc`. The pool doubles if it ever fills, which in practice it doesn't.

**Fixed-point prices.** All prices are `int64_t` in units of 1/10000 of a dollar — the same representation ITCH uses on the wire. No floating-point equality bugs in price level lookups.

**Cache-line-aligned orders.** `Order` is `alignas(64)`. If this engine ever goes multi-threaded, two threads working on adjacent orders won't invalidate each other's cache lines.

**Intrusive price levels.** Within a price level, orders are chained by ID rather than stored in a separate container, so FIFO priority is preserved without extra allocations.

## Project layout

```
src/order_book.cpp    — the book itself: add, cancel, execute, replace
src/itch_parser.cpp   — ITCH 5.0 binary message decoder, zero-alloc
src/detector.cpp      — the four surveillance pattern detectors
src/feed_handler.cpp  — routes messages to per-symbol books
bench/benchmark.cpp   — the numbers above are reproducible here
tests/                — 14 tests, byte-level parser cases included
```

## If I had another month

- True lock-free SPSC queue between feed thread and book thread
- Order book reconstruction replay with PITCH (Cboe) support
- Persist snapshots to a time-series store for post-trade analysis
