#pragma once
#include "Pig.h"

namespace Inferno {
    struct Bitmap2D {
        uint Width, Height;
        List<Palette::Color> Data;

        Palette::Color GetPixel(uint x, uint y) const {
            if (Width == 0 || Height == 0) return {};
            x = std::clamp(x, 0u, Width - 1);
            y = std::clamp(y, 0u, Height - 1);
            return Data[y * Width + x];
        }
    };

    // Reads a BBM packed in an EA IFF interchange file
    Bitmap2D ReadBbm(span<byte> data);
}
