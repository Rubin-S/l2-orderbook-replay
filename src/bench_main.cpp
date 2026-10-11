// bench_main.cpp - step 11: how fast is each book? Four measurements, from
// "how fast can we even read the file" down to "how long does ONE update take".
// usage: mdr_bench <file.evt> [map|array|all]
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <string>
#include <vector>
#include "mdr/books.hpp"
#include "mdr/replay.hpp"

using namespace mdr;
using Clock = std::chrono::steady_clock;          // monotonic: never jumps with NTP

static double secs(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); }
static double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

// Optimizer barrier (same idea as benchmark::DoNotOptimize): pretends to read
// x through memory, so the compiler cannot delete the work that produced it.
template <class T> static void keep(const T& x) { asm volatile("" : : "r"(&x) : "memory"); }

template <L2Book B> struct Null {                  // strategy that does nothing: measures the engine alone
    void on_book(const B&, const Event&) {}
    void on_trade(const B&, const Event&) {}
};
struct Upd { std::int64_t px; std::int64_t qty; Side side; };

template <L2Book B>
static void bench(std::span<const Event> evs, const char* name) {
    constexpr double kMinSec = 2.0;                // repeat each test for >= 2 s and >= 5 passes; report the MEDIAN

    // 1) scan: touch every record, no book. The ceiling: replay can never beat this.
    {
        std::vector<double> r; std::int64_t sum = 0;
        auto start = Clock::now();
        while (secs(start, Clock::now()) < kMinSec || r.size() < 5) {
            auto t0 = Clock::now();
            for (const Event& e : evs) sum += e.qty;
            keep(sum);
            r.push_back(evs.size() / secs(t0, Clock::now()));
        }
        std::printf("scan[%s]    median=%.1fM events/s over %zu passes\n", name, median(r) / 1e6, r.size());
    }

    // 2) replay: the full event loop (snapshot rule, trades, callbacks) with a no-op strategy.
    {
        B book; Null<B> n;
        replay(evs, book, n);                      // warm-up: page cache, first touch of the 16 MB arrays
        std::vector<double> r; auto start = Clock::now();
        while (secs(start, Clock::now()) < kMinSec || r.size() < 5) {
            auto t0 = Clock::now();
            replay(evs, book, n);
            keep(book);
            r.push_back(evs.size() / secs(t0, Clock::now()));
        }
        std::printf("replay[%s]  median=%.2fM events/s over %zu passes\n", name, median(r) / 1e6, r.size());
    }

    // Book updates only, from the first snapshot on (what apply() really sees).
    std::vector<Upd> ups;
    {
        bool have = false;
        for (const Event& e : evs) {
            if (e.kind != Kind::Book) continue;
            if (e.flags & kSnapshot) have = true;
            if (have) ups.push_back({e.px, e.qty, e.side});
        }
    }

    // 3) apply: ONLY book.apply() in a tight loop. clear() between passes is outside the timer.
    {
        B book; std::vector<double> ns;
        for (auto& u : ups) book.apply(u.side, u.px, u.qty);   // warm-up
        auto start = Clock::now();
        while (secs(start, Clock::now()) < kMinSec || ns.size() < 5) {
            book.clear();
            auto t0 = Clock::now();
            for (auto& u : ups) book.apply(u.side, u.px, u.qty);
            auto t1 = Clock::now();
            keep(book);
            ns.push_back(secs(t0, t1) * 1e9 / ups.size());
        }
        std::printf("apply[%s]   median=%.2f ns/update (%.1fM updates/s) over %zu passes\n",
                    name, median(ns), 1e3 / median(ns), ns.size());
    }

    // 4) latency: a timer around EACH apply(). Averages hide spikes; p99.9 shows them.
    //    The timer has its own cost, so we also time an EMPTY timer and print both.
    {
        B book; std::vector<std::uint32_t> lat; lat.reserve(ups.size());
        for (auto& u : ups) {
            auto t0 = Clock::now(); book.apply(u.side, u.px, u.qty); auto t1 = Clock::now();
            lat.push_back(static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
        }
        std::vector<std::uint32_t> ovh(ups.size());
        for (auto& x : ovh) {
            auto a = Clock::now(); auto b = Clock::now();
            x = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
        }
        auto pct = [](std::vector<std::uint32_t> v, double p) {           // copy on purpose: nth_element reorders
            auto k = static_cast<std::size_t>(p * (v.size() - 1));
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
            return v[k];
        };
        std::printf("latency[%s] timer-inclusive p50=%uns p99=%uns p99.9=%uns | empty-timer p50=%uns p99=%uns p99.9=%uns\n",
                    name, pct(lat, .5), pct(lat, .99), pct(lat, .999), pct(ovh, .5), pct(ovh, .99), pct(ovh, .999));
    }
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) { std::fprintf(stderr, "usage: mdr_bench <file.evt> [map|array|all]\n"); return 2; }
    try {
        EventFile f(argv[1]);
        std::string which = argc == 3 ? argv[2] : "all";
        std::printf("events=%zu\n", f.events().size());
        if (which == "map" || which == "all")   bench<MapBook>(f.events(), "map");
        if (which == "array" || which == "all") bench<ArrayBook>(f.events(), "array");
        if (which != "map" && which != "array" && which != "all") { std::fprintf(stderr, "unknown book: %s\n", which.c_str()); return 2; }
    } catch (const std::exception& ex) { std::fprintf(stderr, "error: %s\n", ex.what()); return 1; }
}