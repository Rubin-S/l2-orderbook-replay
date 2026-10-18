// ofi_test.cpp - hand-worked cases for the OFI formula.
// Each case is one situation you can draw on paper: what happened to the
// queue, and what sign OFI must have. If any fails, every result is wrong.
#include <cstdint>
#include <cstdio>
#include "mdr/ofi.hpp"

using namespace mdr;

static int failures = 0;
static void check(const char* name, std::int64_t got, std::int64_t want) {
    bool ok = got == want;
    if (!ok) ++failures;
    std::printf("%s %-42s got %5lld want %5lld\n", ok ? "PASS" : "FAIL", name, (long long)got, (long long)want);
}

int main() {
    const Level A{1001, 7};                         // an ask level that does not move
    const Level B{1000, 5};                         // a bid level that does not move

    // Bid side (ask held still, so ofi = bid_flow)
    check("1 bid same price, size 5->8 (buyers add)",   level_ofi({1000, 5}, {1000, 8}, A, A),  +3);
    check("2 bid same price, size 5->2 (cancel/fill)",  level_ofi({1000, 5}, {1000, 2}, A, A),  -3);
    check("3 bid price UP 999->1000",                   level_ofi({999, 5},  {1000, 4}, A, A),  +4);
    check("4 bid price DOWN 1000->999",                 level_ofi({1000, 5}, {999, 9},  A, A),  -5);

    // Ask side (bid held still, so ofi = -ask_flow)
    check("5 ask same price, size 7->10 (sellers add)", level_ofi(B, B, {1001, 7}, {1001, 10}), -3);
    check("6 ask same price, size 7->2 (cancel/fill)",  level_ofi(B, B, {1001, 7}, {1001, 2}),  +5);
    check("7 ask price DOWN 1002->1001",                level_ofi(B, B, {1002, 7}, {1001, 6}),  -6);
    check("8 ask price UP 1001->1002",                  level_ofi(B, B, {1001, 7}, {1002, 3}),  +7);

    // Edge case: a bid level that did not exist appears
    check("9 bid level appears from empty",             level_ofi({kWorstBid, 0}, {998, 4}, A, A), +4);

    // Nothing changed must give exactly zero
    check("0 nothing changed",                          level_ofi(B, B, A, A), 0);

    std::printf(failures ? "\n%d FAILED\n" : "\nall OFI cases passed\n", failures);
    return failures ? 1 : 0;
}
