#pragma once

#include "Types.h"

namespace Inferno {
    enum class PowerupFlag : uint32 {
        Invulnerable = 1 << 0, // Do not use. Replaced by per-object state.
        BlueKey = 1 << 1,
        RedKey = 1 << 2,
        GoldKey = 1 << 3,
        Flag = 1 << 4, // Carrying flag, for CTF mode
        MapEnemies = 1 << 5, // Show enemies on the map, unused
        FullMap = 1 << 6,
        AmmoRack = 1 << 7,
        Converter = 1 << 8, // Energy to shield converter
        FullMapCheat = 1 << 9, // Same as full map, except unexplored areas aren't blue
        QuadFire = 1 << 10,
        Cloak = 1 << 11,
        Afterburner = 1 << 12,
        Headlight = 1 << 13,
        HeadlightOn = 1 << 14
    };
}
