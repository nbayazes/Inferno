#pragma once

namespace OpenSimplex2 {
    float Noise2(long long seed, double x, double y);
    float Noise3_UnrotatedBase(long long seed, double xr, double yr, double zr);
    void Init();
}

