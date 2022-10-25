#include "pch.h"
#include "Utility.h"
#include <random>

namespace Inferno {
    constexpr int RANDOM_MAX = 0x7FFFFFFFUL;
    std::random_device rd; //seed
    std::mt19937 gen(rd()); //seed for rd(Mersenne twister)
    std::uniform_int_distribution<> randomRange(0, RANDOM_MAX);

    float Random() {
        //return (float)rand() / RAND_MAX;
        return (float)randomRange(gen) / RANDOM_MAX;
    }
}
