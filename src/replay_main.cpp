// replay_main.cpp - step 7: replay a .evt file through MapBook and count events.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include "mdr/books.hpp"
#include "mdr/replay.hpp"

using namespace mdr;

// A minimal Strategy: counts what the replay loop hands it.
struct Counter {
    std::uint64_t book = 0, trades = 0, resets = 0;
    bool prev_snap = false;
    void on_book(const MapBook&, const Event& e) {
        ++book;
        bool snap = e.flags & kSnapshot;
        if (snap && !prev_snap) ++resets;   // same false -> true rule as replay()
        prev_snap = snap;
    }
    void on_trade(const MapBook&, const Event&) { ++trades; }
};

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: mdr_replay <file.evt>\n"); return 2; }
    try {
        EventFile f(argv[1]);                              // throws on a bad file
        const FileHeader& h = f.header();
        MapBook book; Counter c;

        auto t0 = std::chrono::steady_clock::now();
        replay(f.events(), book, c);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        Level bb = book.best_bid(), ba = book.best_ask();
        double ps = h.px_scale, qs = h.qty_scale;          // ticks/lots -> USDT/BTC for printing only
        std::printf("events=%u book_applied=%llu trades=%llu resets=%llu seconds=%.3f events_per_sec=%.0f\n",
                    h.count, (unsigned long long)c.book, (unsigned long long)c.trades,
                    (unsigned long long)c.resets, s, h.count / s);
        std::printf("final: bid %.1f x %.3f | ask %.1f x %.3f | spread %.1f\n",
                    bb.px / ps, bb.qty / qs, ba.px / ps, ba.qty / qs, (ba.px - bb.px) / ps);
    } catch (const std::exception& ex) { std::fprintf(stderr, "error: %s\n", ex.what()); return 1; }
}