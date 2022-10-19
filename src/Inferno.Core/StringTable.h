#pragma once

#include "Types.h"

namespace Inferno {
    // todo: D1 and D2 have different table entries...
    enum class StringTableEntry {
        Score = 52,

        Laser = 104,
        Vulcan = 105,
        Spreadfire = 106,
        Plasma = 107,
        Fusion = 108,
        SuperLaser = 109,
        // ... other primaries

        Concussion = 114,
        // ... other secondaries

        LaserShort = 124,
        // ... other primaries
        ConcussionShort = 134,
        // ... other secondaries

        DontHave = 145, // For primary weapons
        HaveNo = 147, // For secondary weapons
        Sx = 149,
        CantOpenDoor = 152, // 132 for d1

        Lvl = 329, // Laser lvl
        Quad = 330,
    };
}