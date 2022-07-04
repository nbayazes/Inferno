#pragma once

#include "Streams.h"

namespace Inferno {
    // Descent 3 Outrage Graphics File (OGF)
    struct OutrageBitmap {
        int Width, Height;
        int Type;
        List<uint> Data;
        int MipLevels = 1;
        int BitsPerPixel;
        bool UpsideDown;
        string Name;
        
        static OutrageBitmap Read(StreamReader& r);
    };

}