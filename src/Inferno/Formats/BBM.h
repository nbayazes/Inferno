#pragma once
#include "Pig.h"

namespace Inferno {
    struct Bitmap2D {
        uint Width, Height;
        List<Palette::Color> Data;
    };

    // Reads an EA IFF interchange file
    Bitmap2D ReadIff(span<byte> data);
}
