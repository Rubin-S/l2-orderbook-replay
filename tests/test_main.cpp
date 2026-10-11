
#include <cstdio>
#include <string_view>
#include "mdr/parse.hpp"

using namespace mdr;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", \
    __FILE__, __LINE__, #c); ++failures; } } while (0)

static bool throws(std::string_view s, std::int64_t scale) {
    try { parse_scaled(s, scale); } catch (...) { return true; }
    return false;
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

    if (failures == 0) std::puts("all tests passed");
    return failures == 0 ? 0 : 1;   // ctest reads this exit code
}