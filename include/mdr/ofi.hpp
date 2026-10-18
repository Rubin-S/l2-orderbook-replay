// ofi.hpp - the Order Flow Imbalance formula, on its own so it can be tested.
//
// Cont-Kukanov-Stoikov (2014), applied per level (Xu-Yang-Kukanov 2019).
// Compare one level of the book BEFORE (old) and AFTER (new) a message:
//
//   bid_flow = (Pb_new >= Pb_old ? qb_new : 0) - (Pb_new <= Pb_old ? qb_old : 0)
//   ask_flow = (Pa_new <= Pa_old ? qa_new : 0) - (Pa_new >= Pa_old ? qa_old : 0)
//   ofi      = bid_flow - ask_flow     (> 0 buying pressure, < 0 selling pressure)
//
// Prices are integer ticks, so "same price" is an exact compare.
// A missing level is an empty queue at the worst possible price.
#pragma once
#include <cstdint>
#include <limits>
#include "mdr/books.hpp"

namespace mdr {

inline constexpr int kOfiDepth = 10;
inline constexpr std::int64_t kWorstBid = std::numeric_limits<std::int64_t>::min();
inline constexpr std::int64_t kWorstAsk = std::numeric_limits<std::int64_t>::max();

// Buying pressure added on the bid side of one level.
inline std::int64_t bid_flow(Level o, Level n) {
    return (n.px >= o.px ? n.qty : 0) - (n.px <= o.px ? o.qty : 0);
}
// Selling pressure added on the ask side of one level.
inline std::int64_t ask_flow(Level o, Level n) {
    return (n.px <= o.px ? n.qty : 0) - (n.px >= o.px ? o.qty : 0);
}
inline std::int64_t level_ofi(Level bid_old, Level bid_new, Level ask_old, Level ask_new) {
    return bid_flow(bid_old, bid_new) - ask_flow(ask_old, ask_new);
}

}  // namespace mdr
