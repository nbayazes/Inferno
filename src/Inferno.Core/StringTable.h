#pragma once

namespace Inferno {
    // todo: D1 and D2 have different table entries...
    enum class GameString {
        Blue = 12,
        Red = 13,
        Yellow = 14,
        AccessDenied = 15,
        AccessGranted = 16,
        BoostedTo = 17,
        Energy = 18,
        Shield = 19,

        MaxedOut = 21,
        QuadLasers = 22,
        AlreadyHave = 23,
        VulcanAmmo = 24,
        AlreadyAre = 26,
        Cloaked = 27,
        CloakingDevice = 28,
        Invulnerable = 29,
        Invulnerability = 30,

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

        CantOpenDoorD1 = 132,
        CantOpenDoor = 152,

        Lvl = 329, // Laser lvl
        Quad = 330,

        HostageRescued = 465
    };
}