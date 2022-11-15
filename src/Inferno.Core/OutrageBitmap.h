#pragma once

#include "Streams.h"

namespace Inferno::Outrage {
    // Descent 3 Outrage Graphics File (OGF)
    struct Bitmap {
        int Width{}, Height{};
        int Type{};
        List<List<uint>> Mips;
        int BitsPerPixel{};
        string Name;
        
        static Bitmap Read(StreamReader& r); // Read OGF
    };

    // Descent 3 Outrage Animation File (OAF). VClips are OGFs with an extra header.
    struct VClip {
        List<Bitmap> Frames;
        float FrameTime{};
        int Version{};
        bool PingPong{};
        string FileName;

        static VClip Read(StreamReader& r);
    };

}