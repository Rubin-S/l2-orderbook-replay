// books.hpp - L2 (market-by-price) books behind one interface.
// L2: for each price, the TOTAL quantity resting there. Each update says
// "level P now holds Q" (absolute, not a delta). Q == 0 removes the level.
#pragma once
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>
#include "event.hpp"

namespace mdr {

struct Level { std::int64_t px; std::int64_t qty; };
inline constexpr std::int64_t kNoPx = 0;  // sentinel: side is empty

template <class B>
concept L2Book = requires(B b, const B cb, Side s, std::int64_t v, Level* out, int n) {
    { b.apply(s, v, v) } -> std::same_as<void>;   // set level (qty 0 = erase)
    { b.clear() } -> std::same_as<void>;
    { cb.best_bid() } -> std::same_as<Level>;
    { cb.best_ask() } -> std::same_as<Level>;
    { cb.top(s, out, n) } -> std::same_as<int>;   // copy best n levels, return count
};

// MapBook - textbook baseline. std::map = red-black tree: O(log n) per update,
// but every node is a separate heap allocation -> pointer chasing, cache misses.
class MapBook {
    std::map<std::int64_t, std::int64_t, std::greater<>> bids_;  // highest first
    std::map<std::int64_t, std::int64_t, std::less<>>    asks_;  // lowest first

    template <class M> static void set(M& m, std::int64_t px, std::int64_t q) {
        if (q == 0) m.erase(px); else m[px] = q;
    }
    template <class M> static int copy(const M& m, Level* out, int n) {
        int i = 0;
        for (auto it = m.begin(); it != m.end() && i < n; ++it) out[i++] = {it->first, it->second};
        return i;
    }
public:
    void apply(Side s, std::int64_t px, std::int64_t q) {
        if (s == Side::Bid) set(bids_, px, q); else set(asks_, px, q);
    }
    void clear() { bids_.clear(); asks_.clear(); }
    Level best_bid() const { return bids_.empty() ? Level{kNoPx, 0} : Level{bids_.begin()->first, bids_.begin()->second}; }
    Level best_ask() const { return asks_.empty() ? Level{kNoPx, 0} : Level{asks_.begin()->first, asks_.begin()->second}; }
    int top(Side s, Level* out, int n) const { return s == Side::Bid ? copy(bids_, out, n) : copy(asks_, out, n); }
};

static_assert(L2Book<MapBook>);

}  // namespace mdr