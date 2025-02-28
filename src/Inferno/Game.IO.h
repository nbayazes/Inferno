#pragma once
#include "HogFile.h"
#include "Mission.h"

namespace Inferno::Editor {
    struct NewLevelInfo;
}

namespace Inferno::Game {
    // Tries to read the mission file (msn / mn2) for a mission.
    Option<MissionInfo> GetMissionInfo(const HogFile& mission);

    // Tries to read the mission file for the currently loaded mission
    Option<MissionInfo> GetCurrentMissionInfo();

    void NewLevel(Editor::NewLevelInfo& info);

    // Loads a hog from a path. Returns false on error.
    bool LoadMission(const filesystem::path& file);

    // Loads a level from a mission or file
    // If levelName is provided, tries to load that level from the mission, otherwise the first level
    void LoadLevel(const filesystem::path& path, string_view hogEntry, bool autosave = false);

    void EditorLoadLevel(const filesystem::path& path, string_view hogEntry = "");

    // Loads a specific level number from a mission. Shows any briefings if present.
    // Pass > 0 for normal levels, < 0 for secret.
    void LoadLevelFromMission(const MissionInfo& mission, int levelNumber, bool showBriefing = true, bool autosave = false);

    // Checks if a level has been queued to load
    void CheckLoadLevel();

    MissionInfo CreateDescent1Mission(bool isDemo);

    string LevelNameByIndex(int index);

    // Plays music for the level based on its number
    void PlayLevelMusic();

    // Plays a specific music file. Extension is optional.
    // Non-level songs include: briefing, credits, descent, endgame, endlevel
    void PlayMusic(string_view song, bool loop = true);

}
