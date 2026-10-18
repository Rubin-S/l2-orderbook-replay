// ofi_main.cpp - replay a .evt file and export per-level Order Flow Imbalance.
//
// One row per exchange MESSAGE (events sharing ts_local) that touched the book.
// For each level m = 1..10 we compare the book BEFORE and AFTER the message
// and apply the CKS formula from mdr/ofi.hpp (tested in tests/ofi_test.cpp).
//
// reset_flag = 1 on the first row after a snapshot, or after a message we
// could not use (crossed / one-sided book). Its e_m are set to 0: the change
// across a rebuild is not real order flow.
// The summary line also prints throughput: replay + OFI + CSV formatting,
// timed in-process (file loading excluded). Write to /dev/null to time the
// compute without disk.
// usage: mdr_ofi <file.evt> <out.csv> [map|array]
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include "mdr/books.hpp"
#include "mdr/ofi.hpp"
#include "mdr/replay.hpp"

using namespace mdr;

constexpr int kDepth = kOfiDepth;

struct Snap { Level bid[kDepth], ask[kDepth]; };    // top 10 of each side, missing levels filled

template <L2Book B>
static Snap snap(const B& b) {
    Snap s;
    int nb = b.top(Side::Bid, s.bid, kDepth), na = b.top(Side::Ask, s.ask, kDepth);
    for (int i = nb; i < kDepth; ++i) s.bid[i] = {kWorstBid, 0};
    for (int i = na; i < kDepth; ++i) s.ask[i] = {kWorstAsk, 0};
    return s;
}

template <L2Book B>
struct OfiWriter {
    std::FILE* fp = nullptr;
    double pxs = 10, qs = 1000;
    std::int64_t group = -1, group_exch = 0;
    bool group_has_book = false, group_snapshot = false;
    bool have_prev = false;                         // is `prev` a valid, usable book?
    Snap prev{};
    std::uint64_t rows = 0, resets = 0, skipped = 0;

    void end_group(const B& b) {
        if (!group_has_book) return;                // trade-only message: book unchanged
        Level bb = b.best_bid(), ba = b.best_ask();
        if (bb.px == kNoPx || ba.px == kNoPx || bb.px >= ba.px) { ++skipped; have_prev = false; return; }

        Snap cur = snap(b);
        bool reset = group_snapshot || !have_prev;
        std::int64_t e[kDepth] = {};
        if (!reset)
            for (int m = 0; m < kDepth; ++m) e[m] = level_ofi(prev.bid[m], cur.bid[m], prev.ask[m], cur.ask[m]);

        double bp = bb.px / pxs, ap = ba.px / pxs;
        std::fprintf(fp, "%lld,%lld,%.10g,%.8g,%.8g,%.8g", (long long)group_exch, (long long)group,
                     (bp + ap) / 2, ap - bp, bb.qty / qs, ba.qty / qs);
        for (int m = 0; m < kDepth; ++m) std::fprintf(fp, ",%.8g", e[m] / qs);
        std::fprintf(fp, ",%d\n", reset ? 1 : 0);
        ++rows; if (reset) ++resets;
        prev = cur; have_prev = true;
    }
    void before_event(const B& b, const Event& e) {  // BEFORE e is applied
        if (group != -1 && e.ts_local != group) { end_group(b); group_has_book = false; group_snapshot = false; }
        group = e.ts_local;
    }
    void on_book(const B&, const Event& e) {
        group_has_book = true; group_exch = e.ts_exch;
        if (e.flags & kSnapshot) group_snapshot = true;
    }
    void on_trade(const B&, const Event&) {}
    void finish(const B& b) { if (group != -1) end_group(b); }
};

template <L2Book B>
static int run(const EventFile& f, const std::string& out, const char* name) {
    std::FILE* fp = std::fopen(out.c_str(), "w");
    if (!fp) { std::fprintf(stderr, "cannot open %s\n", out.c_str()); return 1; }
    std::fprintf(fp, "ts_exch_us,ts_local_us,mid,spread,bq1,aq1");
    for (int m = 1; m <= kDepth; ++m) std::fprintf(fp, ",e%d", m);
    std::fprintf(fp, ",reset_flag\n");
    B book;
    OfiWriter<B> s;
    s.fp = fp; s.pxs = f.header().px_scale; s.qs = f.header().qty_scale;
    const auto t0 = std::chrono::steady_clock::now();
    replay(f.events(), book, s);
    s.finish(book);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fclose(fp);
    const double n_ev = static_cast<double>(f.events().size());
    std::printf("[%s] rows=%llu resets=%llu skipped=%llu -> %s\n", name, (unsigned long long)s.rows,
                (unsigned long long)s.resets, (unsigned long long)s.skipped, out.c_str());
    std::printf("[%s] %.0f events in %.3f s = %.2fM events/s, %.0f ns per output row\n", name, n_ev, sec,
                n_ev / sec / 1e6, sec * 1e9 / static_cast<double>(s.rows));
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) { std::fprintf(stderr, "usage: mdr_ofi <file.evt> <out.csv> [map|array]\n"); return 2; }
    try {
        EventFile f(argv[1]);
        std::string book = argc == 4 ? argv[3] : "array";
        if (book == "map")   return run<MapBook>(f, argv[2], "map");
        if (book == "array") return run<ArrayBook>(f, argv[2], "array");
        std::fprintf(stderr, "unknown book: %s (use map or array)\n", book.c_str()); return 2;
    } catch (const std::exception& ex) { std::fprintf(stderr, "error: %s\n", ex.what()); return 1; }
}
