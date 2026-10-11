
#include <cstdio>
#include <string_view>
#include "mdr/parse.hpp"
#include "mdr/books.hpp"

using namespace mdr;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", \
    __FILE__, __LINE__, #c); ++failures; } } while (0)

static bool throws(std::string_view s, std::int64_t scale) {
    try { parse_scaled(s, scale); } catch (...) { return true; }
    return false;
}

// Edge cases every L2 book must pass. Templated so FlatBook/ArrayBook reuse it.
template <L2Book B> static void edge_cases() {
    constexpr std::int64_t kFar = 100'000;                          // 10,000 USDT away
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

int main() {
    CHECK(parse_scaled("83569.4", 10)  == 835694);
    CHECK(parse_scaled("83569.40", 10) == 835694);   // trailing zero ok
    CHECK(parse_scaled("83573", 10)    == 835730);   // no dot
    CHECK(parse_scaled("0.019", 1000)  == 19);       // a real amount
    CHECK(parse_scaled("-1.5", 10)     == -15);
    CHECK(throws("0.019", 10));      // your trace: finer than the scale
    CHECK(throws("83569.45", 10));   // finer than a tick: never round
    CHECK(throws("", 10));
    CHECK(throws("1.2.3", 10));
    CHECK(throws("abc", 10));
    CHECK(throws("1.", 10));
    CHECK(throws("--1", 10));
    CHECK(throws("99999999999999999999", 10));    // too big for int64
    CHECK(throws("922337203685477580.7", 100));   // overflows after scaling
    CHECK(decimals("83569.40") == 2 && decimals("83569") == 0);

    edge_cases<MapBook>();  

    if (failures == 0) std::puts("all tests passed");
    return failures == 0 ? 0 : 1;   // ctest reads this exit code
}