#pragma once

#include "Types.h"
#include "Weapon.h"

namespace Inferno {
    enum class PowerupFlag : uint32 {
        Invulnerable = 1 << 0,
        BlueKey = 1 << 1,
        RedKey = 1 << 2,
        GoldKey = 1 << 3,
        Flag = 1 << 4, // Carrying flag, for CTF mode
        MapEnemies = 1 << 5, // Show enemies on the map, unused
        FullMap = 1 << 6,
        AmmoRack = 1 << 7,
        Converter = 1 << 8, // Energy to shield converter
        FullMapCheat = 1 << 9, // Same as full map, except unexplored areas aren't blue
        QuadLasers = 1 << 10,
        Cloaked = 1 << 11,
        Afterburner = 1 << 12,
        Headlight = 1 << 13,
        HeadlightOn = 1 << 14
    };

    struct Player {
        static constexpr int MAX_PRIMARY_WEAPONS = 10;
        static constexpr int MAX_SECONDARY_WEAPONS = 10;
        static constexpr int CALLSIGN_LEN = 8; // so can be used as a 8.3 file name

        ObjID ID = ObjID(0);       // What object number this player is

        struct {
            char Callsign[CALLSIGN_LEN + 1];
            uint8 Address[4];
            uint16 Port;
            bool Connected;
            int PacketsGot, PacketsSent;
            short KillGoal; // when Kills >= Kill goal game ends
            short Deaths;
            short Kills;
        } Net;

        // Game data
        PowerupFlag Powerups;
        float Energy = 100;
        float Shields = 100;
        uint8 Lives;
        int8 Level;             // Level the player is in. Negative for secret levels
        uint8 LaserLevel;       // 0 to 5
        int8 StartingLevel;     // Level the player started the mission on. Used for final score screen.
        ObjID KilledBy = ObjID::None; // Used for multiplayer kill messages, but also gets set by robots
        uint16 PrimaryWeapons;    // Each bit represents an owned primary weapon
        uint16 SecondaryWeapons;  // Each bit represents an owned secondary weapon
        uint16 PrimaryAmmo[MAX_PRIMARY_WEAPONS];
        uint16 SecondaryAmmo[MAX_SECONDARY_WEAPONS];

        int Score;
        int64 LevelTime, TotalTime;

        float CloakTime;
        float InvulnTime;

        struct {
            short Kills;            // Robots killed this level. Used to prevent matcens from spawning too many robots.
            short TotalKills;       // Total kills across all levels. Used for scoring
            short Robots;           // Number of initial robots this level. Used to prevent matcens from spawning too many robots. Why is this here?
            short TotalRobots;      // Number of robots total. Used for final score ratio.
            uint16 TotalRescuedHostages; // Total hostages rescued by the player.
            uint16 TotalHostages;   // Total hostages in all levels. Used for final score ratio
            uint8 HostagesOnShip;   // How many poor sods get killed when ship is lost
            uint8 HostageOnLevel;   // Why is this here?
        } Stats;

        float HomingObjectDist; // Distance of nearest homing object. Used for lock indicators.

        float RearmTime = 1.0f; // Time to swap between weapons and being able to fire

        // Extracted player state that was scattered across methods or globals as static variables
        struct {
            PrimaryWeaponIndex Primary = PrimaryWeaponIndex::Laser;
            SecondaryWeaponIndex Secondary = SecondaryWeaponIndex::Concussion;

            float PrimarySwapTime = 0; // Primary weapon is changing. Used to fade monitor contents.
            float SecondarySwapTime = 0; // Secondary weapon is changing. Used to fade monitor contents.
            float FusionCharge = 0; // How long fusion has been held down
            float OmegaCharge = 1; // How much charge the omega has stored
            float OmegaRechargeDelay = 0; // Delay before Omega starts recharging after firing
            float FlareDelay = 0;
            float PrimaryDelay = 0;
            float SecondaryDelay = 0;
            float AfterburnerCharge = 1; // 0 to 1
            bool HasSpew = false; // has dropped items on death
            bool SpawnInvuln = false; // temporary invuln when spawning
            bool LavafallHissPlaying = false; // checks if a lavafall (or waterfall) sound is already playing
            uint8 MissileGunpoint = 0; // used to alternate left/right missile pods
            uint8 SpreadfireToggle = 0; // horizontal / vertical
            uint8 HelixOrientation = 0; // increments in 22.5 degrees
        } State;

        void GiveWeapon(PrimaryWeaponIndex weapon) {
            PrimaryWeapons |= (1 << (uint16)weapon);
        }

        void GiveWeapon(SecondaryWeaponIndex weapon) {
            SecondaryWeapons |= (1 << (uint16)weapon);
            SecondaryAmmo[(uint16)weapon]++;
        }

        bool HasWeapon(PrimaryWeaponIndex weapon) const {
            return PrimaryWeapons & (1 << (uint16)weapon);
        }

        bool HasWeapon(SecondaryWeaponIndex weapon) const {
            return SecondaryWeapons & (1 << (uint16)weapon);
        }

        void GivePowerup(PowerupFlag powerup) {
            Powerups = (PowerupFlag)((uint32)Powerups | (uint32)powerup);
        }

        bool HasPowerup(PowerupFlag powerup) {
            return (bool)((uint32)Powerups & (uint32)powerup);
        }

        WeaponID GetPrimaryWeaponID() {
            if (State.Primary == PrimaryWeaponIndex::Laser) {
                if (LaserLevel < 4) return WeaponID{ (int)WeaponID::Laser1 + LaserLevel };
                if (LaserLevel == 4) return WeaponID::Laser5;
                if (LaserLevel == 5) return WeaponID::Laser6;
            }

            return PrimaryToWeaponID[(int)State.Primary];
        }

        WeaponID GetSecondaryWeaponID() {
            return SecondaryToWeaponID[(int)State.Secondary];
        }

        bool CanFirePrimary(const Weapon& weapon) {
            auto index = State.Primary;
            if (!HasWeapon(index)) return false;
            if (State.PrimaryDelay > 0) return false;

            bool canFire = true;

            if (index == PrimaryWeaponIndex::Vulcan ||
                index == PrimaryWeaponIndex::Gauss)
                canFire &= weapon.AmmoUsage <= PrimaryAmmo[(int)PrimaryWeaponIndex::Vulcan];

            if (index == PrimaryWeaponIndex::Omega)
                canFire &= Energy > 0 || State.OmegaCharge > 0;

            canFire &= weapon.EnergyUsage <= Energy;
            return canFire;
        }

        bool CanFireSecondary(const Weapon& weapon) {
            auto index = State.Secondary;
            if (!HasWeapon(index)) return false;
            if (State.SecondaryDelay > 0) return false;

            return
                weapon.AmmoUsage <= SecondaryAmmo[(int)index] &&
                weapon.EnergyUsage <= Energy;
        }
    };

}
