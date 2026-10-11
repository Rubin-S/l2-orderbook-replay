// convert.cpp - Tardis CSV (incremental_book_L2 + trades) -> versioned binary .evt
// usage: mdr_convert <l2.csv> <trades.csv> <out.evt>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include "mdr/event.hpp"
#include "mdr/parse.hpp"

using namespace mdr;

namespace {
struct Raw { std::string_view ts, lts, snap, side, px, qty; };

// L2:     exchange,symbol,timestamp,local_timestamp,is_snapshot,side,price,amount
// trades: exchange,symbol,timestamp,local_timestamp,id,side,price,amount
Raw split(std::string_view l, bool is_l2) {
    Raw r; next_field(l); next_field(l);            // skip exchange, symbol
    r.ts = next_field(l); r.lts = next_field(l);
    auto f5 = next_field(l); if (is_l2) r.snap = f5; // is_snapshot OR trade id
    r.side = next_field(l); r.px = next_field(l); r.qty = next_field(l);
    if (r.qty.empty() || !l.empty()) throw std::runtime_error("malformed row (expected 8 fields)");
    return r;
}

struct Stats { int px_dec = 0, qty_dec = 0; std::size_t rows = 0, backwards = 0;
               std::int64_t last_lts = std::numeric_limits<std::int64_t>::min(); };

// Pass 1: max decimals + count rows whose local_timestamp went backwards.
void inspect(const char* path, bool is_l2, Stats& st) {
    std::ifstream in(path); std::string line; std::getline(in, line);  // skip header
    while (std::getline(in, line)) {
        Raw r = split(line, is_l2);
        st.px_dec = std::max(st.px_dec, decimals(r.px));
        st.qty_dec = std::max(st.qty_dec, decimals(r.qty));
        std::int64_t lts = parse_int(r.lts);
        if (lts < st.last_lts) ++st.backwards;
        st.last_lts = lts; ++st.rows;
    }
}

std::int64_t pow10(int d) { std::int64_t v = 1; while (d--) v *= 10; return v; }

// Read one row into an Event. Returns false at EOF.
bool read(std::ifstream& in, std::string& buf, bool is_l2, std::int64_t pxs, std::int64_t qs, Event& e) {
    if (!std::getline(in, buf)) return false;
    Raw r = split(buf, is_l2);
    e = Event{};
    e.ts_exch = parse_int(r.ts);
    e.ts_local = parse_int(r.lts);
    e.px = parse_scaled(r.px, pxs);
    std::int64_t q = parse_scaled(r.qty, qs);
    if (q < 0 || q > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("quantity out of int32 range: " + std::string(r.qty));
    e.qty = static_cast<std::int32_t>(q);
    if (is_l2) {
        e.kind = Kind::Book;
        e.flags = r.snap == "true" ? kSnapshot : 0;
        e.side = r.side == "bid" ? Side::Bid : Side::Ask;
    } else {
        e.kind = Kind::Trade;
        e.side = r.side == "buy" ? Side::Bid : Side::Ask;  // aggressor side
    }
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "usage: mdr_convert <l2.csv> <trades.csv> <out.evt>\n"; return 2; }
    try {
        // ---- pass 1: inspect ----
        Stats sb, st;
        inspect(argv[1], true, sb); inspect(argv[2], false, st);
        int pxd = std::max(sb.px_dec, st.px_dec), qd = std::max(sb.qty_dec, st.qty_dec);
        std::int64_t pxs = pow10(pxd), qs = pow10(qd);
        std::printf("inspect: l2_rows=%zu trade_rows=%zu price_decimals=%d amount_decimals=%d -> px_scale=%lld qty_scale=%lld\n",
                    sb.rows, st.rows, pxd, qd, (long long)pxs, (long long)qs);
        if (sb.backwards || st.backwards) {
            std::fprintf(stderr, "local_timestamp went backwards (l2=%zu, trades=%zu rows)\n", sb.backwards, st.backwards);
            return 1;
        }
        if (sb.rows + st.rows > std::numeric_limits<std::uint32_t>::max()) { std::fprintf(stderr, "too many rows for v1 header\n"); return 1; }

        // ---- pass 2: convert ----
        std::ifstream l2(argv[1]), tr(argv[2]);
        std::ofstream out(argv[3], std::ios::binary);
        if (!l2 || !tr || !out) { std::cerr << "cannot open files\n"; return 2; }
        FileHeader h{}; std::memcpy(h.magic, kMagic, 8); h.version = kVersion;
        h.px_scale = static_cast<std::uint32_t>(pxs); h.qty_scale = static_cast<std::uint32_t>(qs);
        h.count = static_cast<std::uint32_t>(sb.rows + st.rows);
        h.min_book_px = std::numeric_limits<std::int64_t>::max();
        out.write(reinterpret_cast<const char*>(&h), sizeof h);  // placeholder; rewritten at the end

        std::string b1, b2, hdr; std::getline(l2, hdr); std::getline(tr, hdr);
        auto t0 = std::chrono::steady_clock::now();
        Event a{}, b{};
        bool ha = read(l2, b1, true, pxs, qs, a), hb = read(tr, b2, false, pxs, qs, b);
        while (ha || hb) {   // two-way merge by ts_local; book wins ties
            if (ha && (!hb || a.ts_local <= b.ts_local)) {
                h.min_book_px = std::min(h.min_book_px, a.px);
                out.write(reinterpret_cast<const char*>(&a), sizeof a);
                ha = read(l2, b1, true, pxs, qs, a);
            } else {
                out.write(reinterpret_cast<const char*>(&b), sizeof b);
                hb = read(tr, b2, false, pxs, qs, b);
            }
        }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        out.seekp(0); out.write(reinterpret_cast<const char*>(&h), sizeof h);
        std::printf("convert: events=%u seconds=%.3f parse_and_write_rows_per_sec=%.0f\n", h.count, s, h.count / s);
    } catch (const std::exception& ex) { std::fprintf(stderr, "error: %s\n", ex.what()); return 1; }
}