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

    // Serialized player info
    struct PlayerData {
        static constexpr int MAX_PRIMARY_WEAPONS = 10;
        static constexpr int MAX_SECONDARY_WEAPONS = 10;
        static constexpr int CALLSIGN_LEN = 8; // so can be used as a 8.3 file name
        static constexpr int INITIAL_LIVES = 3;

        ObjRef Reference = { ObjID{0}, ObjSig{0} }; // Reference to player

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
        uint8 Lives = INITIAL_LIVES;
        int8 Level;             // Level the player is in. Negative for secret levels
        uint8 LaserLevel;       // 0 to 5
        int8 StartingLevel;     // Level the player started the mission on. Used for final score screen.
        ObjRef KilledBy = {}; // Used for multiplayer kill messages, but also gets set by robots
        uint16 PrimaryWeapons;    // Each bit represents an owned primary weapon
        uint16 SecondaryWeapons;  // Each bit represents an owned secondary weapon
        std::array<uint16, MAX_PRIMARY_WEAPONS>  PrimaryAmmo;
        std::array<uint16, MAX_SECONDARY_WEAPONS> SecondaryAmmo;

        int Score, LevelStartScore;
        int64 LevelTime, TotalTime;

        struct {
            int16 Kills = 0;            // Robots killed this level. Used to prevent matcens from spawning too many robots.
            int16 TotalKills = 0;       // Total kills across all levels. Used for scoring
            int16 Robots = 0;           // Number of initial robots this level. Used to prevent matcens from spawning too many robots. Why is this here?
            int16 TotalRobots = 0;      // Number of robots total. Used for final score ratio.
            uint16 TotalHostages = 0;   // Total hostages in all levels. Used for final score ratio
            uint8 HostagesOnLevel = 0;   // Why is this here?
        } Stats;

        uint16 HostagesRescued; // Hostages rescued by the player on the current level.
        uint8 HostagesOnboard;   // How many poor souls get killed when ship is lost
        float HomingObjectDist = -1; // Distance of nearest homing object. Used for lock indicators.
    };

}
