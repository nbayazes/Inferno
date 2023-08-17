#pragma once
#include "Types.h"

namespace Inferno {
    enum class SoundUID : unsigned int { None = 0 }; // ID used to cancel a playing sound

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

    constexpr ObjRef GLOBAL_SOUND_SOURCE = { ObjID(9999), ObjSig::None }; // Assign the source to this value to have it culled against all others
    constexpr float DEFAULT_SOUND_RADIUS = 250;
}