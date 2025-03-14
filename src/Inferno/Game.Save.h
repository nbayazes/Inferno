#pragma once
#include "Difficulty.h"
#include "Game.Player.h"
#include "Weapon.h"

namespace Inferno {
    struct SaveGameInfo {
        int version = 1;
        bool autosave; // marks this save as an autosave, which means it will get replaced automatically
        int levelNumber;
        string levelName;
        string missionName;
        string missionPath;
        string dateTime;
        int64 timestamp; // unix timestamp of when this save was created
        int64 missionTimestamp; // used to associate multiple saves with a single run

        DifficultyLevel difficulty;

        float shields, energy;

        uint16 primaryWeapons, secondaryWeapons;

        std::array<uint16, Player::MAX_PRIMARY_WEAPONS> primaryAmmo;
        std::array<uint16, Player::MAX_SECONDARY_WEAPONS> secondaryAmmo;

        PrimaryWeaponIndex primary;
        SecondaryWeaponIndex secondary;
        PowerupFlag powerups;
        uint8 bombIndex;
        uint8 laserLevel;

        int lives;
        Player::Stats stats;

        filesystem::path saveFilePath; // Path of a loaded save
    };

    // Formats a unix timestamp into a string
    string FormatTimestamp(int64 ticks);

    string GetSaveName();


    uint64 GetTimestamp();

    // Saves the current game state to a given file name.
    // If missionTimestamp is provided, it will associate the save with other saves using that timestamp.
    // Returns the timestamp of the save
    int64 SaveGame(string_view name, int64 missionTimestamp = 0, bool autosave = false);

    void DeleteSave(const SaveGameInfo& save);

    // Prunes autosaves in the save folder to a maximum
    void PruneAutosaves(uint maxAutosaves);

    int64 CreateAutosave(int64 missionTimestamp = 0, uint maxAutosaves = 3);

    void WriteSave(const filesystem::path& path, const SaveGameInfo& save);
    Option<SaveGameInfo> ReadSave(string_view name);
    List<SaveGameInfo> ReadAllSaves();
    bool LoadSave(const SaveGameInfo& save);
}
