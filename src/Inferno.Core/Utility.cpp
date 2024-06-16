#include "pch.h"
#include "Utility.h"
#include <random>

namespace Inferno {
    namespace {
        constexpr int RANDOM_MAX = std::numeric_limits<int>::max();
        std::mt19937 gen; //seed for rd(Mersenne twister)
        std::uniform_int_distribution randomRange(0, RANDOM_MAX);
    }

    std::mt19937& InternalMt19937() { return gen; }

    void InitRandom() {
        gen = std::mt19937(std::random_device{}());
    }

    float Random() {
        return (float)randomRange(gen) / (float)RANDOM_MAX;
    }

    int RandomInt(int maximum) {
        std::uniform_int_distribution range(0, maximum);
        return range(gen);
    }

    int RandomInt(int minimum, int maximum) {
        std::uniform_int_distribution range(minimum, maximum);
        return range(gen);
    }
}
