#pragma once

#include "Streams.h"
#include "OutrageTable.h"

namespace Inferno::Outrage {
    // Descent 3 Outrage Graphics File (OGF)
    struct Bitmap {
        int Width, Height;
        int Type;
        List<List<uint>> Mips;
        int BitsPerPixel;
        bool UpsideDown;
        string Name;
        
        static Bitmap Read(StreamReader& r);
    };

    // Descent 3 VClips are bitmaps with an extra header (OAF)
    struct VClip {
        List<Bitmap> Frames;
        float FrameTime;
        int Version;
        static VClip Read(StreamReader& r, const TextureInfo& ti);
    };

}