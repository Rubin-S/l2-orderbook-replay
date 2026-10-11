#pragma once
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mdr {
    inline std::string_view next_field(std::string_view&line){
        auto c = line.find(',');
        auto f = line.substr(0, c);
        line = (c == std::string_view::npos) ? std::string_view{}:line.substr(c+1);
        return f;
    }

    inline std::int64_t parse_int(std::string_view s){
        std::int64_t v{};
        if (s.empty()) throw std:: runtime_error("Empty integer field");
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc{} || p != s.data() + s.size())
            throw std::runtime_error("bad int: "+ std::string(s));
        return v;
    }

    inline int decimals(std::string_view s) {
        auto dot = s.find('.');
        return dot == std::string_view::npos ? 0 : static_cast<int>(s.size() - dot -1);
    }

    inline std::int64_t parse_scaled(std::string_view s, std::int64_t scale) {
        if (s.empty()) throw std::runtime_error("empty decimal field");

        bool neg = s.front() == '-';
        if (neg) s.remove_prefix(1);

        auto dot = s.find('.');
        auto whole_s = s.substr(0, dot);
        if(whole_s.empty() || whole_s.front() == '-' || whole_s.front() == '+')
            throw std::runtime_error("Bad decimal");
        std::int64_t whole = parse_int(whole_s);

        std::int64_t frac = 0, fscale = 1;
        if (dot !=std::string_view::npos) {
            auto fs = s.substr(dot + 1);
            if(fs.empty()|| fs.size() > 18)
                throw std::runtime_error("Bad decimal:" + std::string(s));
            for (char ch : fs){
                if (ch < '0' || ch > '9')
                    throw std::runtime_error("Bad decimal: " + std::string(s));
                frac = frac * 10 + (ch - '0');
                fscale *= 10;
            }

            while (fscale > scale && frac % 10 == 0) { frac /= 10; fscale /= 10; }

            if (fscale > scale)
                throw std::runtime_error("unrepresentable at this scale:" + std::string(s));
        }

        std::int64_t r;
        if(__builtin_mul_overflow(whole, scale, &r)||
            __builtin_add_overflow(r, frac * (scale / fscale), &r))
            throw std::runtime_error("overflow: " + std::string(s));
        return neg ? -r : r;
    }
}