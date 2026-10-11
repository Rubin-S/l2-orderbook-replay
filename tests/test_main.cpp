// test_main.cpp - dependency-free tests. CHECK stays active in Release (no assert()).
//  1) exact decimal parsing, incl. malformed input and overflow
//  2) each book on hand-written edge cases (incl. ArrayBook's overflow path)
//  3) differential: random updates into MapBook (oracle) and ArrayBook
#include <cstdint>
#include <cstdio>
#include <random>
#include <string_view>
#include <vector>
#include "mdr/books.hpp"
#include "mdr/parse.hpp"

using namespace mdr;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", \
    __FILE__, __LINE__, #c); ++failures; } } while (0)

static bool throws(std::string_view s, std::int64_t scale) {
    try { parse_scaled(s, scale); } catch (...) { return true; }
    return false;
}

// Edge cases every L2 book must pass. Templated so every book reuses it.
template <L2Book B> static void edge_cases() {
    constexpr std::int64_t kFar = 3 * ArrayBook::kWidth;            // OUTSIDE ArrayBook's window
    B b;
    Level out[5];
    CHECK(b.best_bid().px == kNoPx && b.best_ask().px == kNoPx);   // empty book
    CHECK(b.top(Side::Bid, out, 5) == 0);

    b.apply(Side::Bid, 1000, 5); b.apply(Side::Bid, 1002, 7); b.apply(Side::Ask, 1005, 3);
    CHECK(b.best_bid().px == 1002 && b.best_bid().qty == 7);       // higher bid is best

    b.apply(Side::Bid, 1002, 9);                                    // absolute update, not delta
    CHECK(b.best_bid().qty == 9);

    b.apply(Side::Bid, 1002, 0);                                    // delete best -> next level
    CHECK(b.best_bid().px == 1000);

    b.apply(Side::Bid, 999, 0);                                     // delete missing level: no-op
    CHECK(b.best_bid().px == 1000 && b.top(Side::Bid, out, 5) == 1);

    b.apply(Side::Ask, 1004, 1);                                    // new better ask
    CHECK(b.best_ask().px == 1004);
    CHECK(b.top(Side::Ask, out, 5) == 2 && out[0].px == 1004 && out[1].px == 1005);

    b.apply(Side::Bid, 1000 - kFar, 4);                             // far-away levels stay visible
    b.apply(Side::Ask, 1005 + kFar, 6);
    CHECK(b.top(Side::Bid, out, 5) == 2 && out[1].px == 1000 - kFar);
    CHECK(b.top(Side::Ask, out, 5) == 3 && out[2].px == 1005 + kFar);

    b.apply(Side::Bid, 1000, 0);                                    // only the far bid remains
    CHECK(b.best_bid().px == 1000 - kFar && b.best_bid().qty == 4);

    b.clear();                                                      // snapshot reset
    CHECK(b.best_bid().px == kNoPx && b.best_ask().px == kNoPx);
}

// Full side as a vector (for full-depth comparison).
template <L2Book B> static std::vector<Level> all(const B& b, Side s) {
    std::vector<Level> v(100000); v.resize(b.top(s, v.data(), 100000)); return v;
}

// True if two books agree: best_*, top 10, and optionally the FULL depth.
template <L2Book A, L2Book B> static bool same_book(const A& x, const B& y, bool full) {
    for (Side s : {Side::Bid, Side::Ask}) {
        Level bx = s == Side::Bid ? x.best_bid() : x.best_ask();
        Level by = s == Side::Bid ? y.best_bid() : y.best_ask();
        if (bx.px != by.px || bx.qty != by.qty) return false;
        Level lx[10], ly[10];
        int nx = x.top(s, lx, 10), ny = y.top(s, ly, 10);
        if (nx != ny) return false;
        for (int k = 0; k < nx; ++k) if (lx[k].px != ly[k].px || lx[k].qty != ly[k].qty) return false;
        if (nx > 0 && lx[0].px != bx.px) return false;              // best_* must agree with top()
        if (full) {
            auto vx = all(x, s), vy = all(y, s);
            if (vx.size() != vy.size()) return false;
            for (std::size_t k = 0; k < vx.size(); ++k)
                if (vx[k].px != vy[k].px || vx[k].qty != vy[k].qty) return false;
        }
    }
    return true;
}

// Differential test: same random updates into MapBook (oracle) and ArrayBook.
static void differential() {
    for (std::uint64_t seed : {1ull, 42ull, 2026ull}) {
        MapBook m; ArrayBook a;
        std::mt19937_64 rng(seed);                                  // fixed seeds: failures reproduce
        std::int64_t mid = 835000;                                  // ~83,500 USDT in ticks
        for (int i = 0; i < 300'000; ++i) {
            mid += static_cast<std::int64_t>(rng() % 3) - 1;        // random-walk the market
            if (rng() % 50'000 == 0) { m.clear(); a.clear(); }      // occasional snapshot reset
            Side s = rng() & 1 ? Side::Bid : Side::Ask;
            std::int64_t dist = rng() % 100 == 0
                ? static_cast<std::int64_t>(rng() % (4 * ArrayBook::kWidth))   // 1%: far away
                : static_cast<std::int64_t>(rng() % 200);                      // 99%: near the touch
            std::int64_t px = s == Side::Bid ? mid - 1 - dist : mid + 1 + dist;
            std::int64_t q = rng() % 4 == 0 ? 0 : static_cast<std::int64_t>(rng() % 5000 + 1);  // 25% deletes
            m.apply(s, px, q); a.apply(s, px, q);
            if (!same_book(m, a, i % 1000 == 0)) {                  // full depth every 1000 ops
                std::fprintf(stderr, "differential mismatch: seed %llu op %d\n", (unsigned long long)seed, i);
                ++failures; break;
            }
        }
    }
}

int main() {
    // ---- parser ----
    CHECK(parse_scaled("83569.4", 10)  == 835694);
    CHECK(parse_scaled("83569.40", 10) == 835694);   // trailing zero ok
    CHECK(parse_scaled("83573", 10)    == 835730);   // no dot
    CHECK(parse_scaled("0.019", 1000)  == 19);       // a real amount
    CHECK(parse_scaled("-1.5", 10)     == -15);
    CHECK(throws("0.019", 10));      // finer than the scale
    CHECK(throws("83569.45", 10));   // finer than a tick: never round
    CHECK(throws("", 10));
    CHECK(throws("1.2.3", 10));
    CHECK(throws("abc", 10));
    CHECK(throws("1.", 10));
    CHECK(throws("--1", 10));
    CHECK(throws("99999999999999999999", 10));    // too big for int64
    CHECK(throws("922337203685477580.7", 100));   // overflows after scaling
    CHECK(decimals("83569.40") == 2 && decimals("83569") == 0);

    // ---- books ----
    edge_cases<MapBook>();
    edge_cases<ArrayBook>();
    differential();

    if (failures == 0) std::puts("all tests passed");
    return failures == 0 ? 0 : 1;   // ctest reads this exit code
}