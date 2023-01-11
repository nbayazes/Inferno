#pragma once

#include "Streams.h"

namespace Inferno::Outrage {
    // Descent 3 Outrage Graphics File (OGF)
    struct Bitmap {
        int Width = 0, Height = 0;
        int Type = 0;
        List<List<uint>> Mips;
        int BitsPerPixel = 0;
        string Name;
        
        static Bitmap Read(StreamReader& r); // Read OGF
    };

    // Descent 3 VClips are bitmaps with an extra header (OAF)
    struct VClip {
        List<Bitmap> Frames;
        float FrameTime;
        int Version;
        bool PingPong;
        string FileName;

        static VClip Read(StreamReader& r);
    };
}