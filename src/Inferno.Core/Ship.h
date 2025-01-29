#pragma once
#include <bitset>
#include "Types.h"
#include "Weapon.h"

namespace Inferno {

    constexpr auto MAX_GUNPOINTS = 8;
    constexpr auto MAX_WEAPONS = 20;

    struct WeaponBattery {
        //float EnergyUsage = 0; // Energy per shot
        //float AmmoUsage = 0; // Ammo per shot
        //int AmmoType = -1;
        //int GunpointWeapons[8]{}; // Weapon IDs to use for each gunpoint
        string WeaponName; // Used to resolve weapon id, refer to weapon entry names.
        WeaponID Weapon = WeaponID::None;
        float SequenceResetTime = 0;
        //bool Gunpoints[8]{};

        // Crosshair should be determined by gunpoints
        struct FiringInfo {
            std::bitset<MAX_GUNPOINTS> Gunpoints;
            float Delay = 0.25f; // Delay between shots
        };

        List<FiringInfo> Firing; // Cycles through each entry after firing.

        std::bitset<MAX_GUNPOINTS> QuadGunpoints; // Gunpoints to use with quad upgrade
        uint16 MaxAmmo = 0;
    };

    struct ShipInfo {
#pragma region HAM properties
        ModelID Model;
        VClipID ExplosionVClip{};
        float Mass, Drag;
        float MaxThrust, ReverseThrust, Brakes;
        float Wiggle;
        float MaxRotationalThrust;
        Array<Vector3, MAX_GUNPOINTS> Gunpoints{};
#pragma endregion

        string Name;
        float DamageTaken = 1.0f; // Multiplier on damage taken
        float EnergyMultiplier = 1.0f;
        float TurnRollScale = 2.0f;
        float TurnRollRate = 0.8f;

        std::array<WeaponBattery, MAX_WEAPONS> Weapons; // 10 primaries, 10 secondaries
    };
}
