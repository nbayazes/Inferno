#pragma once
#include "Level.h"
#include "HogFile.h"
#include "Mission.h"
#include "Room.h"

namespace Inferno::Game {
    // The loaded level. Only one level can be active at a time.
    inline Inferno::Level Level{ 1, WallsSerialization::STANDARD };

    // The loaded mission. Not always present.
    inline Option<HogFile> Mission;

    // Only single player for now
    inline PlayerData Player = {};

    // is the game level loading?
    inline std::atomic<bool> IsLoading = false;

    void LoadLevel(Inferno::Level&&);

    void LoadMission(const filesystem::path& file);

    inline void UnloadMission() {
        Mission = {};
    }

    // Tries to read the mission file (msn / mn2) for the loaded mission
    Option<MissionInfo> TryReadMissionInfo();

    // Game time elapsed in seconds
    inline double ElapsedTime = 0;

    inline List<Room> Rooms;
}
