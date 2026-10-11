// replay.hpp - memory-mapped event file + the event loop.
#pragma once
#include <chrono>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "books.hpp"
#include "event.hpp"

namespace mdr {

// Read-only memory map of a .evt file. The kernel pages the file in on demand
// and we read records straight out of the page cache: no read() buffers, no
// per-event allocation.
class EventFile {
    void* p_ = nullptr; std::size_t n_ = 0;
public:
    explicit EventFile(const std::string& path) {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) throw std::runtime_error("open " + path);
        struct stat st{}; ::fstat(fd, &st);
        n_ = static_cast<std::size_t>(st.st_size);
        if (n_ < sizeof(FileHeader) || n_ % sizeof(Event)) { ::close(fd); throw std::runtime_error("not a .evt file (size)"); }
        p_ = ::mmap(nullptr, n_, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);                                   // the mapping stays valid after close
        if (p_ == MAP_FAILED) throw std::runtime_error("mmap failed");
        ::madvise(p_, n_, MADV_SEQUENTIAL);            // hint: front-to-back access
        const FileHeader& h = header();
        if (std::memcmp(h.magic, kMagic, 8) != 0) throw std::runtime_error("bad magic");
        if (h.version != kVersion) throw std::runtime_error("unsupported version");
        if (h.count != n_ / sizeof(Event) - 1) throw std::runtime_error("header count does not match file size");
    }
    ~EventFile() { if (p_ && p_ != MAP_FAILED) ::munmap(p_, n_); }
    EventFile(const EventFile&) = delete; EventFile& operator=(const EventFile&) = delete;
    const FileHeader& header() const { return *static_cast<const FileHeader*>(p_); }
    std::span<const Event> events() const { return {static_cast<const Event*>(p_) + 1, n_ / sizeof(Event) - 1}; }
};

// Anything that wants market data implements these callbacks. Optional:
// before_event(book, e), called before e is applied (the validator needs it).
template <class S, class B>
concept Strategy = requires(S s, const B& b, const Event& e) {
    { s.on_book(b, e) };    // after a book update is applied
    { s.on_trade(b, e) };   // for each trade
};

struct ReplayOptions { double speed = 0; };  // 0 = max speed; 10 = 10x real time

// Snapshot rule (Tardis docs): whenever is_snapshot flips false -> true, a new
// snapshot batch starts: discard the whole book. Happens at the start of each
// day and after every feed reconnect. Updates before the first snapshot are
// skipped (state unknown).
template <L2Book B, Strategy<B> S>
void replay(std::span<const Event> evs, B& book, S& strat, ReplayOptions opt = {}) {
    bool in_snapshot = false, have_snapshot = false;
    const auto wall0 = std::chrono::steady_clock::now();
    const std::int64_t t0 = evs.empty() ? 0 : evs.front().ts_local;
    for (const Event& e : evs) {
        if constexpr (requires { strat.before_event(book, e); }) strat.before_event(book, e);
        if (opt.speed > 0) {
            auto due = wall0 + std::chrono::microseconds(static_cast<std::int64_t>((e.ts_local - t0) / opt.speed));
            std::this_thread::sleep_until(due);
        }
        if (e.kind == Kind::Trade) { if (have_snapshot) strat.on_trade(book, e); continue; }
        const bool snap = e.flags & kSnapshot;
        if (snap && !in_snapshot) { book.clear(); have_snapshot = true; }   // false -> true: reset
        in_snapshot = snap;
        if (!have_snapshot) continue;
        book.apply(e.side, e.px, e.qty);
        strat.on_book(book, e);
    }
}

}  // namespace mdr