#pragma once

#include "Streams.h"

namespace Inferno::Outrage {
    // Descent 3 Outrage Graphics File (OGF)
    struct Bitmap {
        int Width, Height;
        int Type;
        List<List<uint>> Mips;
        int BitsPerPixel;
        string Name;
        
        static Bitmap Read(StreamReader& r, bool skipData = false); // Read OGF
        static Bitmap ReadPig(StreamReader& r);
    };

    // Descent 3 VClips are bitmaps with an extra header (OAF)
    struct VClip {
        List<Bitmap> Frames;
        float FrameTime;
        int Version;
        bool PingPong;

        static VClip Read(StreamReader& r, bool skipData = false);
    };

}