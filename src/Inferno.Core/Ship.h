#pragma once
#include <bitset>
#include "Types.h"
#include "Weapon.h"

namespace Inferno {

    constexpr auto MAX_GUNPOINTS = 8;
    constexpr auto MAX_WEAPONS = 20;

    struct WeaponBattery {
        float EnergyUsage = 0; // Energy per shot
        uint16 AmmoUsage = 0; // Ammo per shot
        int16 AmmoType = -1; // For primary weapons, ammo type to use. For sharing ammo between two weapons.
        string AmmoName; // Name to show when picking up ammo for this slot
        //int GunpointWeapons[8]{}; // Weapon IDs to use for each gunpoint
        string WeaponName; // Used to resolve weapon id, refer to weapon entry names.
        WeaponID Weapon = WeaponID::None;
        float SequenceResetTime = 0;
        //bool Gunpoints[8]{};

        // Crosshair should be determined by gunpoints
        struct FiringInfo {
            std::bitset<MAX_GUNPOINTS> Gunpoints = 0b00000011;
            float Delay = 0.25f; // Delay between shots
        };

        uint8 FiringCount = 1; // Number of entries in Firing to use
        std::array<FiringInfo, 10> Firing; // Cycles through each entry after firing.

        std::bitset<MAX_GUNPOINTS> QuadGunpoints; // Gunpoints to use with quad upgrade
        uint16 Ammo = 0; // Maximum rounds or missiles carried
        uint16 RackAmmo = 0; // Maximum rounds or missiles carried
    };

    struct ShipInfo {
#pragma region HAM properties
        ModelID Model;
        string ModelName;
        string DestroyedModelName;
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
