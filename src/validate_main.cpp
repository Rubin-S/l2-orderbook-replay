// validate_main.cpp - steps 8 + 9.4: replay a .evt file through MapBook or ArrayBook
// and VALIDATE every complete message against Tardis book_snapshot_5.
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include "mdr/books.hpp"
#include "mdr/parse.hpp"
#include "mdr/replay.hpp"

using namespace mdr;

// ---- the reference: one book_snapshot_5 row = Tardis's top 5 after a message ----
struct Top5 {
    std::array<Level, 5> bid{}, ask{};
    int nb = 0, na = 0;
    bool operator==(const Top5& o) const {
        if (nb != o.nb || na != o.na) return false;
        for (int i = 0; i < nb; ++i) if (bid[i].px != o.bid[i].px || bid[i].qty != o.bid[i].qty) return false;
        for (int i = 0; i < na; ++i) if (ask[i].px != o.ask[i].px || ask[i].qty != o.ask[i].qty) return false;
        return true;
    }
};
struct Snap { std::int64_t lts; Top5 top; };

// exchange,symbol,timestamp,local_timestamp, then 5x (ask px, ask amt, bid px, bid amt)
static std::vector<Snap> load_snaps(const std::string& path, std::int64_t pxs, std::int64_t qs) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("open " + path);
    std::string line; std::getline(in, line);                  // skip header
    std::vector<Snap> v;
    while (std::getline(in, line)) {
        std::string_view l = line; Snap s{};
        next_field(l); next_field(l); next_field(l);
        s.lts = parse_int(next_field(l));
        for (int i = 0; i < 5; ++i) {
            auto ap = next_field(l), aq = next_field(l), bp = next_field(l), bq = next_field(l);
            if (!ap.empty()) s.top.ask[s.top.na++] = {parse_scaled(ap, pxs), parse_scaled(aq, qs)};
            if (!bp.empty()) s.top.bid[s.top.nb++] = {parse_scaled(bp, pxs), parse_scaled(bq, qs)};
        }
        v.push_back(s);
    }
    return v;
}

template <L2Book B> static Top5 top5(const B& b) {
    Top5 t;
    t.nb = b.top(Side::Bid, t.bid.data(), 5);
    t.na = b.top(Side::Ask, t.ask.data(), 5);
    return t;
}

// ---- the validator: a Strategy that checks the book once per MESSAGE ----
// Rows sharing a local_timestamp are one exchange message; the book is only
// meaningful after the WHOLE message is applied. So we notice a message has
// ended when the next event has a different local_timestamp (before_event),
// and once more at EOF (finish).
template <L2Book B>
struct Validator {
    std::vector<Snap> snaps; std::size_t si = 0;     // reference rows + read cursor
    std::int64_t group = -1;                          // local_timestamp of the open message
    bool group_has_book = false, started = false;
    Top5 prev{};                                      // our top 5 after the previous message
    std::uint64_t groups = 0, matched = 0, content_mismatch = 0,
                  changed_no_row = 0, row_no_change = 0, crossed = 0;

    void end_group(const B& b) {
        if (!group_has_book) return;                  // trade-only message: book unchanged
        ++groups;
        Top5 cur = top5(b);
        Level bb = b.best_bid(), ba = b.best_ask();
        if (bb.px != kNoPx && ba.px != kNoPx && bb.px >= ba.px) ++crossed;

        while (si < snaps.size() && snaps[si].lts < group) ++si;   // skip older rows
        const Snap* row = nullptr;
        while (si < snaps.size() && snaps[si].lts == group) row = &snaps[si++];  // last row of this message

        bool changed = !started || !(cur == prev);
        if (row) {
            if (cur == row->top) ++matched;           // same top 5 as Tardis
            else if (!changed) ++row_no_change;       // Tardis moved, we didn't
            else ++content_mismatch;                  // both moved, to different books
        } else if (changed && started) {
            ++changed_no_row;                         // we moved, Tardis wrote no row
        }
        prev = cur; started = true;
    }
    void before_event(const B& b, const Event& e) {  // called BEFORE e is applied
        if (group != -1 && e.ts_local != group) { end_group(b); group_has_book = false; }
        group = e.ts_local;
    }
    void on_book(const B&, const Event&) { group_has_book = true; }
    void on_trade(const B&, const Event&) {}
    void finish(const B& b) { if (group != -1) end_group(b); }   // flush the last message
};

// ---- one runner, stamped out per book type ----
template <L2Book B>
static int run(const EventFile& f, const std::string& snap_path, const char* name) {
    const FileHeader& h = f.header();
    B book;
    Validator<B> v;
    v.snaps = load_snaps(snap_path, h.px_scale, h.qty_scale);
    replay(f.events(), book, v);
    v.finish(book);
    std::printf("[%s] snap_rows=%zu message_groups=%llu matched=%llu crossed_groups=%llu\n", name,
                v.snaps.size(), (unsigned long long)v.groups, (unsigned long long)v.matched,
                (unsigned long long)v.crossed);
    std::printf("[%s] content_mismatch=%llu changed_without_row=%llu row_without_change=%llu\n", name,
                (unsigned long long)v.content_mismatch, (unsigned long long)v.changed_no_row,
                (unsigned long long)v.row_no_change);
    if constexpr (std::same_as<B, ArrayBook>)        // compile-time branch: MapBook has no such method
        std::printf("[%s] overflow_updates=%llu\n", name, (unsigned long long)book.overflow_updates());
    bool ok = v.content_mismatch == 0 && v.changed_no_row == 0 && v.row_no_change == 0;
    std::printf(ok ? "[%s] VALID\n" : "[%s] INVALID\n", name);
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) { std::fprintf(stderr, "usage: mdr_validate <file.evt> <snap5.csv> [map|array]\n"); return 2; }
    try {
        EventFile f(argv[1]);
        std::string book = argc == 4 ? argv[3] : "map";
        if (book == "map")   return run<MapBook>(f, argv[2], "map");
        if (book == "array") return run<ArrayBook>(f, argv[2], "array");
        std::fprintf(stderr, "unknown book: %s (use map or array)\n", book.c_str()); return 2;
    } catch (const std::exception& ex) { std::fprintf(stderr, "error: %s\n", ex.what()); return 1; }
}