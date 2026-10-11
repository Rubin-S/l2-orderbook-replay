// features_main.cpp - step 10: replay a .evt file and export features.csv,
// one row per complete, uncrossed message that changes the top of book.
// Projects 2-4 (market making, OFI, execution) read this file instead of raw data.
// usage: mdr_features <file.evt> <out.csv> [map|array]
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include "mdr/books.hpp"
#include "mdr/replay.hpp"

using namespace mdr;

// A Strategy that looks at the book once per MESSAGE (rows sharing a
// local_timestamp), exactly like the validator, and writes one CSV row
// when the best bid or best ask (price or size) changed.
template <L2Book B>
struct Featurizer {
    std::FILE* fp = nullptr;
    double pxs = 10, qs = 1000;                       // from the file header
    std::int64_t group = -1;                          // local_timestamp of the open message
    bool group_has_book = false;
    Level lb{}, la{};                                 // last emitted best bid / ask
    std::uint64_t groups = 0, rows = 0, skipped_crossed = 0, skipped_one_sided = 0;

    void end_group(const B& b) {
        if (!group_has_book) return;                  // trade-only message: book unchanged
        ++groups;
        Level bb = b.best_bid(), ba = b.best_ask();
        if (bb.px == kNoPx || ba.px == kNoPx) { ++skipped_one_sided; return; }   // no mid without both sides
        if (bb.px >= ba.px) { ++skipped_crossed; return; }                       // never emit a crossed book
        if (bb.px == lb.px && bb.qty == lb.qty && ba.px == la.px && ba.qty == la.qty) return;  // top unchanged

        Level bl[5], al[5];
        int nb = b.top(Side::Bid, bl, 5), na = b.top(Side::Ask, al, 5);
        double bq5 = 0, aq5 = 0;
        for (int i = 0; i < nb; ++i) bq5 += bl[i].qty;
        for (int i = 0; i < na; ++i) aq5 += al[i].qty;

        // Integers -> real units only here, at the output boundary.
        double bp = bb.px / pxs, ap = ba.px / pxs, bq = bb.qty / qs, aq = ba.qty / qs;
        double mid   = (bp + ap) / 2;
        double micro = (bp * aq + ap * bq) / (bq + aq);   // size-weighted: leans toward the THINNER side
        double imb5  = (bq5 - aq5) / (bq5 + aq5);         // +1 = all size on bids, -1 = all on asks (top 5)
        std::fprintf(fp, "%lld,%.8g,%.8g,%.10g,%.8g,%.10g,%.6f\n",
                     (long long)group, bp, ap, mid, ap - bp, micro, imb5);
        ++rows; lb = bb; la = ba;
    }
    void before_event(const B& b, const Event& e) {  // called BEFORE e is applied
        if (group != -1 && e.ts_local != group) { end_group(b); group_has_book = false; }
        group = e.ts_local;
    }
    void on_book(const B&, const Event&) { group_has_book = true; }
    void on_trade(const B&, const Event&) {}
    void finish(const B& b) { if (group != -1) end_group(b); }   // flush the last message
};

template <L2Book B>
static int run(const EventFile& f, const std::string& out, const char* name) {
    std::FILE* fp = std::fopen(out.c_str(), "w");
    if (!fp) { std::fprintf(stderr, "cannot open %s\n", out.c_str()); return 1; }
    std::fprintf(fp, "ts_local_us,bid,ask,mid,spread,microprice,imbalance5\n");
    B book;
    Featurizer<B> s;
    s.fp = fp; s.pxs = f.header().px_scale; s.qs = f.header().qty_scale;
    replay(f.events(), book, s);
    s.finish(book);
    std::fclose(fp);
    std::printf("[%s] message_groups=%llu rows=%llu skipped_crossed=%llu skipped_one_sided=%llu -> %s\n", name,
                (unsigned long long)s.groups, (unsigned long long)s.rows,
                (unsigned long long)s.skipped_crossed, (unsigned long long)s.skipped_one_sided, out.c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) { std::fprintf(stderr, "usage: mdr_features <file.evt> <out.csv> [map|array]\n"); return 2; }
    try {
        EventFile f(argv[1]);
        std::string book = argc == 4 ? argv[3] : "array";
        if (book == "map")   return run<MapBook>(f, argv[2], "map");
        if (book == "array") return run<ArrayBook>(f, argv[2], "array");
        std::fprintf(stderr, "unknown book: %s (use map or array)\n", book.c_str()); return 2;
    } catch (const std::exception& ex) { std::fprintf(stderr, "error: %s\n", ex.what()); return 1; }
}