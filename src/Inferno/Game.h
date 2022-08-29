#pragma once
#include "Level.h"
#include "HogFile.h"
#include "Mission.h"

namespace Inferno {
    enum class GameState { Game, Editor, Paused };
}

namespace Inferno::Game {
    constexpr float TICK_RATE = 1.0f / 64; // 64 ticks per second (homing missiles use 32 ticks per second)

    inline int Difficulty = 0; // 0 to 4

    inline GameState State = GameState::Editor;

    // The loaded level. Only one level can be active at a time.
    inline Inferno::Level Level;

    // The loaded mission. Not always present.
    inline Option<HogFile> Mission;

    // Only single player for now
    inline PlayerData Player = {};

    // is the game level loading?
    inline std::atomic<bool> IsLoading = false;

    void LoadLevel(Inferno::Level&&);

    void LoadMission(filesystem::path file);

    inline void UnloadMission() {
        Mission = {};
    }

    // Tries to read the mission file (msn / mn2) for the loaded mission
    Option<MissionInfo> TryReadMissionInfo();

    void FireTestWeapon(Inferno::Level& level, ObjID, int gun, int id);

    // Elapsed game time  in seconds. Stops when paused.
    inline double ElapsedTime = 0;
    inline float LerpAmount = 1; // How much to lerp between the previous and next object states

    void Update(float dt);
    void ToggleEditorMode();

    inline bool ShowDebugOverlay = false;

    inline bool SecretLevelDestroyed = false;
}
