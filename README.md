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
