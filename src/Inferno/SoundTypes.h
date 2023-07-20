#pragma once
#include "Types.h"

namespace Inferno {
    using SoundUID = unsigned int; // ID used to cancel a playing sound

    // Handle to a sound resource
    struct SoundResource {
        int D1 = -1; // Index to PIG data
        int D2 = -1; // Index to S22 data
        string D3; // D3 file name or system path

        // Priority is D3, D1, D2
        bool operator== (const SoundResource& rhs) const {
            if (!D3.empty() && !rhs.D3.empty()) return D3 == rhs.D3;
            if (D1 != -1 && rhs.D1 != -1) return D1 == rhs.D1;
            return D2 == rhs.D2;
        }
    };

    struct Sound2D {
        float Volume = 1;
        float Pitch = 0;
        SoundResource Resource;
    };

    constexpr ObjID GLOBAL_SOUND_SOURCE = ObjID(9999); // Assign the source to this value to have it culled against all others
}