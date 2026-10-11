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

// ---------------------------------------------------------------------------
// MapBook - textbook baseline. std::map = red-black tree: O(log n) per update,
// but every node is a separate heap allocation -> pointer chasing, cache misses.
// Simple enough to trust on sight: it is the ORACLE the fast books must match.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// ArrayBook - dense "hot window" array + std::map "cold overflow".
// In-window prices live in qty[px - base]: an update is one array write, O(1).
// A cached index tracks the best level; deleting the best scans to the next
// non-empty slot (usually a few ticks). Out-of-window prices (e.g. bids at
// 1,000 USDT) go to an ordered overflow map, so NOTHING is dropped.
// kWidth = 2^20 ticks (+/-52,428.8 USDT at a 0.1 tick) = 8 MB per side.
// ---------------------------------------------------------------------------
class ArrayBook {
public:
    static constexpr std::int64_t kWidth = 1 << 20;
private:
    std::vector<std::int64_t> bid_, ask_;
    std::map<std::int64_t, std::int64_t, std::greater<>> obid_;  // out-of-window bids, best first
    std::map<std::int64_t, std::int64_t, std::less<>>    oask_;  // out-of-window asks, best first
    std::int64_t base_ = 0; bool based_ = false;
    std::int64_t bb_ = -1, ba_ = kWidth;   // best indices in the window; -1 / kWidth = none
    std::uint64_t overflow_updates_ = 0;

    bool in_window(std::int64_t px, std::int64_t& i) {
        if (!based_) { base_ = px - kWidth / 2; based_ = true; }   // centre on first price
        i = px - base_;
        return i >= 0 && i < kWidth;
    }
    Level dense_bid() const { return bb_ >= 0 ? Level{base_ + bb_, bid_[bb_]} : Level{kNoPx, 0}; }
    Level dense_ask() const { return ba_ < kWidth ? Level{base_ + ba_, ask_[ba_]} : Level{kNoPx, 0}; }
public:
    ArrayBook() : bid_(kWidth, 0), ask_(kWidth, 0) {}

    void apply(Side s, std::int64_t px, std::int64_t q) {
        std::int64_t i;
        if (!in_window(px, i)) {                                   // slow path: overflow map
            ++overflow_updates_;
            if (s == Side::Bid) { if (q) obid_[px] = q; else obid_.erase(px); }
            else                { if (q) oask_[px] = q; else oask_.erase(px); }
            return;
        }
        if (s == Side::Bid) {                                      // fast path: one array write
            bid_[i] = q;
            if (q != 0 && i > bb_) bb_ = i;                        // new best bid
            else if (q == 0 && i == bb_) { while (bb_ >= 0 && bid_[bb_] == 0) --bb_; }  // best deleted
        } else {
            ask_[i] = q;
            if (q != 0 && i < ba_) ba_ = i;
            else if (q == 0 && i == ba_) { while (ba_ < kWidth && ask_[ba_] == 0) ++ba_; }
        }
    }
    void clear() {  // O(window), but only on snapshot resets
        std::fill(bid_.begin(), bid_.end(), 0); std::fill(ask_.begin(), ask_.end(), 0);
        obid_.clear(); oask_.clear(); bb_ = -1; ba_ = kWidth;
    }
    Level best_bid() const {
        Level d = dense_bid();
        if (obid_.empty()) return d;
        Level o{obid_.begin()->first, obid_.begin()->second};
        return (d.px == kNoPx || o.px > d.px) ? o : d;
    }
    Level best_ask() const {
        Level d = dense_ask();
        if (oask_.empty()) return d;
        Level o{oask_.begin()->first, oask_.begin()->second};
        return (d.px == kNoPx || o.px < d.px) ? o : d;
    }
    // Merge the dense window and the overflow map, best price first.
    int top(Side s, Level* out, int n) const {
        int k = 0;
        if (s == Side::Bid) {
            std::int64_t i = bb_; auto it = obid_.begin();
            while (k < n) {
                while (i >= 0 && bid_[i] == 0) --i;
                bool hd = i >= 0, ho = it != obid_.end();
                if (!hd && !ho) break;
                if (hd && (!ho || base_ + i > it->first)) { out[k++] = {base_ + i, bid_[i]}; --i; }
                else { out[k++] = {it->first, it->second}; ++it; }
            }
        } else {
            std::int64_t i = ba_; auto it = oask_.begin();
            while (k < n) {
                while (i < kWidth && ask_[i] == 0) ++i;
                bool hd = i < kWidth, ho = it != oask_.end();
                if (!hd && !ho) break;
                if (hd && (!ho || base_ + i < it->first)) { out[k++] = {base_ + i, ask_[i]}; ++i; }
                else { out[k++] = {it->first, it->second}; ++it; }
            }
        }
        return k;
    }
    std::uint64_t overflow_updates() const { return overflow_updates_; }
};

static_assert(L2Book<MapBook> && L2Book<ArrayBook>);

}  // namespace mdr