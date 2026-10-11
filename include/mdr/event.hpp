#pragma once
#include <bit>
#include <cstdint>
#include <type_traits>

namespace mdr {
    static_assert(std::endian::native == std::endian::little, "fileformate is little-endian");

    enum class Side : std::uint8_t { Bid = 0, Ask = 1 };
    enum class Kind : std::uint8_t { Book = 0, Trade = 1 };
    enum Flags : std::uint8_t { kSnapshot = 1 };

    struct Event {
        std::int64_t ts_exch;
        std::int64_t ts_local;
        std::int64_t px;
        std::int32_t qty;
        Side side;
        Kind kind;
        std::uint8_t flags;
        std::uint8_t reserved = 0;
    };

    static_assert(sizeof(Event) == 32 && alignof(Event) == 8);
    static_assert(std::is_trivially_copyable_v<Event> &&
                    std::is_standard_layout_v<Event>);

    inline constexpr char kMagic[8] = {'M', 'D', 'R', 'E', 'V', 'T' ,'\0', '\0'};
    inline constexpr std::uint16_t kVersion = 1;

    struct FileHeader {
        char magic[8];
        std::uint16_t version;
        std::uint16_t reserved;
        std::uint32_t px_scale;
        std::uint32_t qty_scale;
        std::uint32_t count;
        std::int64_t min_book_px;
    };
    static_assert(sizeof(FileHeader) == 32 &&
                    std::is_trivially_copyable_v<FileHeader>);
}