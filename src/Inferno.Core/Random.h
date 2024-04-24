#pragma once

#include <random>
#include <ranges>

namespace Inferno {
    ::std::mt19937& InternalMt19937();

    // Randomly shuffles a range in place
    void Shuffle(auto&& range) {
        std::ranges::shuffle(range, InternalMt19937());
    }
}