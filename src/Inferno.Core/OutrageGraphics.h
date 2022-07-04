#pragma once

#include "Streams.h"

namespace Inferno {
    // Descent 3 Outrage Graphics File (OGF)
    struct OutrageGraphics {
        int Width, Height;
        int Type;
        List<ushort> Data; // ushort??
        int MipLevels = 1;
        int BitsPerPixel;
        bool UpsideDown;
        string Name;

        List<int> GetMipData(int mip);
        
        static OutrageGraphics Read(StreamReader& r);
    };

}