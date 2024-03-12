#pragma once
#include "Pig.h"

namespace Inferno {
    struct Bitmap2D {
        uint Width, Height;
        List<Palette::Color> Data;
    };

    // Reads a BBM packed in an EA IFF interchange file
    Bitmap2D ReadBbm(span<byte> data);
}
