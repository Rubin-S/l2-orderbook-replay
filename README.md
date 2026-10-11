# l2-orderbook-replay

A C++20 market-data replay engine that rebuilds the Binance BTCUSDT USDS-M futures **L2 order book** from raw exchange updates, **proves it correct** against an independent reference (Tardis `book_snapshot_5`), and benchmarks two book designs: a textbook `std::map` book and a cache-friendly **ArrayBook** that replays **263M events/s, 12.4× faster**.

It also exports a per-message top-of-book feature file (mid, spread, microprice, top-5 imbalance), the input for my market-making, order-flow-imbalance and execution projects.

Companion to [`orderbook`](https://github.com/Rubin-S/orderbook) (a price-time matching engine): that repo *creates* a book from orders; this one *reconstructs* a real exchange's book from its feed.

## Pipeline
```
Tardis CSV (L2 updates + trades)
   └─ mdr_convert ──▶ btc.evt   (32-byte fixed records, integer ticks/lots, exact decimal parsing)
        └─ replay loop (mmap, snapshot-reset rule, per-message grouping)
             ├─ mdr_validate ──▶ top-5 vs Tardis book_snapshot_5, every message
             ├─ mdr_features ──▶ features.csv (ts, bid, ask, mid, spread, microprice, imbalance5)
             └─ mdr_bench    ──▶ scan / replay / apply / latency, MapBook vs ArrayBook
```

## Design decisions
- **Integers end to end.** Prices and sizes are parsed exactly into int64 ticks and lots (`83569.4` → `835694`). The parser throws instead of rounding, so no floating-point error ever enters the book. Doubles appear only when writing `features.csv`.
- **Binary event file + mmap.** CSV parsing happens once, in `mdr_convert`. Replay reads 32-byte records straight from the page cache, with no per-event allocation.
- **Message-level semantics.** Rows sharing a `local_timestamp` are one exchange message. The book is inspected only after the whole message is applied; mid-message states never existed on the exchange and can be crossed.
- **Snapshot rule.** A false→true flip of `is_snapshot` clears the book (start of day, feed reconnects).
- **One interface, two books.** An `L2Book` C++20 concept. `MapBook` (two `std::map`s) is the simple oracle; `ArrayBook` is a dense 2^20-tick window per side (8 MiB) centred on the first price, with ordered overflow maps for far-away levels (e.g. bids at 1,000 USDT), so nothing is dropped. A cached best index makes updates one array write.
- **Testing in three layers.** Hand-written edge cases per book, a randomized differential test (ArrayBook vs MapBook, 900k ops, full-depth compares), and a mutation check proving the tests catch a broken best-level scan.

## Build and run
Requires GCC 13+ (or Clang 17+), CMake 3.20+, Ninja, curl.
```bash
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure        # parser, edge cases, differential test

scripts/get_data.sh                                # downloads the free sample, builds data/btc.evt
./build/mdr_validate data/btc.evt data/snap5.csv array      # expect VALID
./build/mdr_features data/btc.evt data/features.csv array
./build/mdr_bench    data/btc.evt all              # [map|array|all] [scan|replay|apply|latency|all]
```

## Data
Tardis free first-of-month sample (Binance USDS-M futures, BTCUSDT, 2026-10-01): the first 3,000,000 L2 rows (00:00:00 to 00:38:51 UTC, minus the half-cut final message), with trades and snapshots trimmed to that window. Raw data isn't committed (size and Tardis terms); `scripts/get_data.sh` rebuilds it, and `results/checksums_2026-10-11.txt` lets you confirm byte-identical inputs.

## Provenance
Built step by step from a guided reference implementation. I typed, built, tested and validated every component myself, and every number in this README comes from my own runs on the machine listed under Environment.

## Results

### Correctness
| Check | Result |
|---|---|
| Validator vs Tardis `book_snapshot_5` (MapBook and ArrayBook) | 46,642 / 46,642 rows matched; 0 content mismatches, 0 changed-without-row, 0 row-without-change; 0 crossed books |
| Differential test, ArrayBook vs MapBook | 3 seeds × 300,000 random updates, full-depth compare every 1,000 ops: 0 failures |
| Mutation check | disabling ArrayBook's best-bid rescan fails 3 edge cases and all 3 seeds |
| features.csv, map vs array | 41,894 rows, byte-identical (`cmp`), 0 rows with spread ≤ 0 |

### Performance
| Benchmark | MapBook (`std::map`) | ArrayBook | Speed-up | Method |
|---|---|---|---|---|
| Replay throughput | 21.24 M events/s | 263.05 M events/s | 12.4× | full event loop, no-op strategy; median of ≥ 5 passes and ≥ 2 s |
| `apply()` cost | 50.10 ns/update | 2.96 ns/update | 16.9× | tight loop over 2,999,701 book updates; median of ≥ 5 passes |
| Latency p50 / p99 / p99.9 | 57 / 162 / 261 ns | 11 / 20 / 34 ns | — | `steady_clock` around each `apply()`, **timer-inclusive** (empty timer alone: p50 ≈ 10 ns) |
| Scan ceiling (read only) | 728 M events/s | 669 M events/s | — | sum over all records, no book; same code for both, so the gap is run-to-run noise |

ArrayBook serves 16,394 of 2,999,701 updates (0.55%) from its overflow map, all more than 50,000 USDT from mid.

Raw output: [`results/bench_2026-10-11.txt`](results/bench_2026-10-11.txt)

### Environment
- CPU: 13th Gen Intel(R) Core(TM) i7-1360P
- OS: Fedora 44 x86_64, kernel 7.2.5-200.fc44.x86_64, default power profile
- Compiler: GCC 16.2.1, CMake Release (`-O3 -DNDEBUG`), `-Wall -Wextra -Wpedantic`, C++20
- Data: Binance USDS-M BTCUSDT, Tardis free sample 2026-10-01 00:00:00–00:38:51 UTC; 3,063,267 events (2,999,701 L2 updates + 63,566 trades)
### Why ArrayBook is faster (hardware counters)
`perf stat` on the `apply` benchmark, pinned to one P-core (`taskset -c 2`), user-space only. Per update = count ÷ ((passes + 1) × 2,999,701).

| Per update | MapBook | ArrayBook | Ratio |
|---|---|---|---|
| Cycles | 230.9 | 14.5 | 16× |
| Instructions | 146.3 | 22.3 | 6.6× |
| IPC | 0.63 | 1.54 | 2.4× |
| L1 data-cache misses | 2.948 | 0.011 | 279× |
| Branch misses | 4.685 | 0.201 | 23× |

std::map does 6.6× more instructions (the tree walk) at 2.4× lower IPC: the core stalls on pointer-chasing L1 misses and on mispredicted left/right branches. ArrayBook's update is one indexed store into a few hot cache lines.

Caveat: perf counts the whole process, including `clear()` between passes (outside the timer). ArrayBook's `clear()` rewrites its 16 MB arrays each pass, which most likely dominates its last-level-cache misses (212M vs map's 11.7M), so that counter is omitted. It also accounts for ~11% of ArrayBook's counted cycles (14.5 counted vs 2.81 ns × 4.6 GHz ≈ 12.9 timed).

Raw output: [`results/perf_2026-10-11.txt`](results/perf_2026-10-11.txt)
