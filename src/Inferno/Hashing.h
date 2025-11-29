#pragma once

#include "unordered_dense.h"
#include "Utility.h"

namespace Inferno {
    // Comparator for equality of strings ignoring case
    struct StringEqualsIgnoreCase {
        using is_transparent = int;

        [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const {
            return String::EqualsIgnoreCase(a, b);
        }
    };

    struct StringEquals {
        using is_transparent = int;

        [[nodiscard]] bool operator()(std::string_view a, std::string_view b) const {
            return a == b;
        }
    };

    // hash that works with any string compatible with string_view
    struct StringHash {
        using is_transparent = void; // enable heterogeneous overloads
        using is_avalanching = void; // mark class as high quality avalanching hash

        [[nodiscard]] uint64_t operator()(std::string_view str) const noexcept {
            return ankerl::unordered_dense::hash<std::string_view>{}(str);
        }
    };


    struct StringHashIgnoreCase {
        using is_transparent = void; // enable heterogeneous overloads
        using is_avalanching = void; // mark class as high quality avalanching hash

        [[nodiscard]] uint64_t operator()(std::string_view str) const noexcept {
            return ankerl::unordered_dense::hash<std::string_view>{}(String::ToLower(str));
        }
    };

}