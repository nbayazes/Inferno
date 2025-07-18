#include "pch.h"
#include "Game.Save.h"
#include "Game.h"
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_time.h>
#include "Yaml.h"

using namespace Yaml;

namespace Inferno {
    string FormatTimestamp(int64 ticks) {
        SDL_DateTime dateTime{};
        if (!SDL_TimeToDateTime(ticks, &dateTime, true)) {
            SPDLOG_WARN("Unable to get current datetime");
            return "Unknown Time";
        }

        string month = [&] {
            switch (dateTime.month) {
                default:
                case 1: return "Jan";
                case 2: return "Feb";
                case 3: return "Mar";
                case 4: return "Apr";
                case 5: return "May";
                case 6: return "Jun";
                case 7: return "Jul";
                case 8: return "Aug";
                case 9: return "Sep";
                case 10: return "Oct";
                case 11: return "Nov";
                case 12: return "Dec";
            }
        }();

        SDL_DateFormat dateFormat;
        SDL_TimeFormat timeFormat;
        SDL_GetDateTimeLocalePreferences(&dateFormat, &timeFormat);

        string time;

        if (timeFormat == SDL_TIME_FORMAT_12HR) {
            auto timeLabel = dateTime.hour > 11 ? "PM" : "AM";
            auto hour = dateTime.hour > 12 ? dateTime.hour - 12 : dateTime.hour == 0 ? 12 : dateTime.hour;
            time = fmt::format("{}:{:02} {}", hour, dateTime.minute, timeLabel); // 11:00 AM
        }
        else {
            time = fmt::format("{:02}:{:02}", dateTime.hour, dateTime.minute); // 00:30 AM
        }

        auto date = fmt::format("{} {}, {}", month, dateTime.day, dateTime.year); // Jan 12, 2025
        return fmt::format("{}  {}", date, time);
    }

    SaveGameInfo CreateSave() {
        SDL_Time ticks{};
        if (!SDL_GetCurrentTime(&ticks)) {
            SPDLOG_WARN("Unable to get current time");
        }

        SDL_DateTime dateTime{};
        if (!SDL_TimeToDateTime(ticks, &dateTime, true)) {
            SPDLOG_WARN("Unable to get current datetime");
        }

        auto& player = Game::Player;

        SaveGameInfo save{};
        save.dateTime = FormatTimestamp(ticks);

        //doc["Time"] << fmt::format("{}:{}", dateTime.hour, dateTime.minute);
        save.levelNumber = Game::LevelNumber;
        save.levelName = Game::Level.Name;
        save.timestamp = ticks;

        if (auto mission = Game::GetCurrentMissionInfo()) {
            save.missionName = mission->Name;
            ASSERT(Game::Mission);
            if (Game::Mission)
                save.missionPath = Game::Mission->Path.string();
        }

        save.difficulty = Game::Difficulty;

        save.shields = player.Shields;
        save.energy = player.Energy;

        save.primaryWeapons = player.PrimaryWeapons;
        save.secondaryWeapons = player.SecondaryWeapons;
        save.primaryAmmo = player.PrimaryAmmo;
        save.secondaryAmmo = player.SecondaryAmmo;
        save.primary = player.Primary;
        save.secondary = player.Secondary;
        save.powerups = player.Powerups;
        save.bombIndex = player.BombIndex;
        save.laserLevel = player.LaserLevel;

        save.lives = player.Lives;
        save.stats = player.stats;
        return save;
    }

    template <class T>
    bool ReadSequence(ryml::ConstNodeRef node, span<T> values) {
        if (!node.has_children()) return false;

        int i = 0;
        for (const auto& c : node.children()) {
            if (i >= values.size()) return false;
            if (!ReadValue(c, values[i])) return false;
            i++;
        }

        return true;
    }

    SaveGameInfo ReadSave(ryml::ConstNodeRef node) {
        SaveGameInfo save{};

#define READ_PROP(name) ReadValue2(node, #name, save.##name)
        READ_PROP(version);
        READ_PROP(dateTime);
        READ_PROP(autosave);
        READ_PROP(timestamp);
        READ_PROP(levelNumber);
        READ_PROP(levelName);
        READ_PROP(missionName);
        READ_PROP(missionPath);
        READ_PROP(difficulty);

        READ_PROP(shields);
        READ_PROP(energy);
        READ_PROP(primaryWeapons);
        READ_PROP(secondaryWeapons);

        if (node.has_child("primaryAmmo"))
            ReadSequence(node["primaryAmmo"], span<uint16>{ save.primaryAmmo });

        if (node.has_child("secondaryAmmo"))
            ReadSequence(node["secondaryAmmo"], span<uint16>{ save.secondaryAmmo });

        READ_PROP(primary);
        READ_PROP(secondary);
        READ_PROP(powerups);
        READ_PROP(bombIndex);
        READ_PROP(laserLevel);

        READ_PROP(lives);
#undef READ_PROP

#define READ_STAT(name) ReadValue2(node, #name, save.stats.##name)
        READ_STAT(score);
        READ_STAT(totalKills);
        READ_STAT(totalTime);
        READ_STAT(totalDeaths);
        READ_STAT(totalHostages);
#undef READ_STAT

        return save;
    }

    void WriteSave(const filesystem::path& path, const SaveGameInfo& save) {
        ryml::Tree doc(128, 128);
        doc.rootref() |= ryml::MAP;

        SDL_Time ticks{};
        if (!SDL_GetCurrentTime(&ticks)) {
            SPDLOG_WARN("Unable to get current time");
        }

        SDL_DateTime dateTime{};
        if (!SDL_TimeToDateTime(ticks, &dateTime, true)) {
            SPDLOG_WARN("Unable to get current datetime");
        }

        //auto mission = Game::GetMissionInfo();
#define WRITE_PROP(name) doc[#name] << save.##name
        WRITE_PROP(version);
        WRITE_PROP(dateTime);
        WRITE_PROP(autosave);
        WRITE_PROP(timestamp);
        WRITE_PROP(levelNumber);
        WRITE_PROP(levelName);
        WRITE_PROP(missionName);
        WRITE_PROP(missionPath);
        doc["difficulty"] << ToUnderlying(save.difficulty);

        WRITE_PROP(shields);
        WRITE_PROP(energy);
        WRITE_PROP(primaryWeapons);
        WRITE_PROP(secondaryWeapons);

        Yaml::WriteSequence(doc["primaryAmmo"], save.primaryAmmo);
        Yaml::WriteSequence(doc["secondaryAmmo"], save.secondaryAmmo);

        doc["primary"] << ToUnderlying(save.primary);
        doc["secondary"] << ToUnderlying(save.secondary);
        doc["powerups"] << ToUnderlying(save.powerups);
        WRITE_PROP(bombIndex);
        WRITE_PROP(laserLevel);

        WRITE_PROP(lives);
#undef WRITE_PROP

#define WRITE_STAT(name) doc[#name] << save.stats.##name
        WRITE_STAT(score);
        WRITE_STAT(totalKills);
        WRITE_STAT(totalTime);
        WRITE_STAT(totalDeaths);
        WRITE_STAT(totalHostages);
#undef WRITE_STAT

        filesystem::path temp = path;
        temp.replace_filename("temp.sav");

        {
            std::ofstream file(temp);
            file << doc;
        }

        // Write went okay, ovewrite the old file and remove temp
        filesystem::copy(temp, path, filesystem::copy_options::overwrite_existing);
        filesystem::remove(temp);
        SPDLOG_INFO("Saving game to {}", path.string());
    }

    // Prefer saving into the user
    filesystem::path GetSaveFolder() {
        auto userFolder = SDL_GetUserFolder(SDL_FOLDER_SAVEDGAMES);
        if (userFolder)
            return filesystem::path(userFolder) / "Inferno"; // prefer saving to user folder
        else
            return "saves"; // save to the local directory
    }

    uint64 GetTimestamp() {
        SDL_Time ticks{};
        if (!SDL_GetCurrentTime(&ticks))
            return 0;

        return ticks;
    }

    string GetSaveName() {
        string name = "autosave.sav";

        auto ticks = GetTimestamp();

        SDL_DateTime dateTime{};
        if (!SDL_TimeToDateTime(ticks, &dateTime, true))
            return name;

        return fmt::format("{}-{:02}-{:02}-{:02}{:02}{:02}.sav", dateTime.year, dateTime.month, dateTime.day, dateTime.hour, dateTime.minute, dateTime.second);
    }

    int64 SaveGame(string_view name, int64 missionTimestamp, bool autosave) {
        try {
            if (!Game::Mission) {
                SPDLOG_ERROR("Can only create saves when a mission is loaded");
                return 0;
            }

            auto saveFolder = GetSaveFolder();

            if (!filesystem::exists(saveFolder))
                filesystem::create_directory(saveFolder);

            auto save = CreateSave();
            save.autosave = autosave;
            if (missionTimestamp > 0)
                save.missionTimestamp = missionTimestamp > 0 ? missionTimestamp : save.timestamp;

            WriteSave(saveFolder / name, save);
            return save.timestamp;
        }
        catch (const std::exception& e) {
            auto message = fmt::format("Error saving game:\n{}", e.what());
            ShowErrorMessage(message);
            SPDLOG_ERROR(message);
            return 0;
        }
    }

    void DeleteSave(const SaveGameInfo& save) {
        filesystem::path path = save.saveFilePath;

        if (filesystem::exists(path)) {
            SPDLOG_INFO("Deleting save {}", path.string());
            filesystem::remove(path);
        }
    }

    void PruneAutosaves(uint maxAutosaves) {
        auto saves = ReadAllSaves();
        auto autosaves = Seq::filter(saves, [](const SaveGameInfo& i) { return i.autosave; });

        if (autosaves.size() > maxAutosaves) {
            Seq::sortBy(autosaves, [](const SaveGameInfo& a, const SaveGameInfo& b) {
                return a.timestamp > b.timestamp; // sort oldest last
            });

            while (autosaves.size() > maxAutosaves) {
                DeleteSave(autosaves.back());
                autosaves.pop_back();
            }
        }
    }

    int64 CreateAutosave(int64 missionTimestamp, uint maxAutosaves) {
        auto saveName = GetSaveName();
        auto timestamp = SaveGame(saveName, missionTimestamp, true);
        PruneAutosaves(maxAutosaves);
        return timestamp;
    }

    Option<SaveGameInfo> ReadSave(string_view name) {
        try {
            auto path = GetSaveFolder() / name;

            std::ifstream file(path);
            if (!file) return {};

            std::stringstream buffer;
            buffer << file.rdbuf();
            ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(buffer.str()));
            ryml::NodeRef root = doc.rootref();

            if (!root.is_map()) {
                return {};
            }

            auto save = ReadSave(doc);
            save.saveFilePath = path;
            return save;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error loading save file:\n{}", e.what());
            return {};
        }
    }

    // todo: only read the header info, not the level data (when implemented)
    List<SaveGameInfo> ReadAllSaves() {
        try {
            List<SaveGameInfo> saves;

            for (auto& file : filesystem::directory_iterator(GetSaveFolder())) {
                if (!file.is_regular_file()) continue;

                auto& filePath = file.path();
                if (String::ToLower(filePath.extension().string()) != ".sav")
                    continue;

                if (auto save = ReadSave(filePath.filename().string())) {
                    saves.push_back(*save);
                }
            }

            Seq::sortBy(saves, [](const SaveGameInfo& a, const SaveGameInfo& b) {
                return a.timestamp > b.timestamp;
            });

            return saves;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error read save folder:\n{}", e.what());
            return {};
        }
    }

    bool LoadSave(const SaveGameInfo& save) {
        Game::Difficulty = save.difficulty;

        auto& player = Game::Player;
        player.Energy = save.energy;
        player.Shields = save.shields;

        player.PrimaryWeapons = save.primaryWeapons;
        player.SecondaryWeapons = save.secondaryWeapons;
        player.PrimaryAmmo = save.primaryAmmo;
        player.SecondaryAmmo = save.secondaryAmmo;

        player.Primary = (PrimaryWeaponIndex)save.primary;
        player.Secondary = (SecondaryWeaponIndex)save.secondary;
        player.Powerups = save.powerups;
        player.BombIndex = save.bombIndex;
        player.LaserLevel = save.laserLevel;

        player.Lives = save.lives;
        player.stats = save.stats;

        try {
            if (!filesystem::exists(save.missionPath)) {
                ShowErrorMessage(fmt::format("Unable to find {}", save.missionPath));
                return false;
            }

            if (Game::LoadMission(save.missionPath)) {
                // Game::Mission should be set if LoadMission succeeds
                auto info = Game::GetMissionInfo(*Game::Mission);
                if (!info) {
                    ShowErrorMessage(fmt::format("Mission info for {} not found", save.missionPath));
                    return false;
                }

                Game::LoadLevelFromMission(*info, save.levelNumber, false);
                Game::MissionTimestamp = save.missionTimestamp;
                SPDLOG_INFO("Loading save {} with mission timestamp of {}", save.saveFilePath.string(), save.missionTimestamp);
                return true;
            }
            else {
                ShowErrorMessage(fmt::format("Error loading {}", save.missionPath));
                return false;
            }
        }
        catch (const std::exception& e) {
            ShowErrorMessage(fmt::format("Error loading save:\n{}", e.what()));
            return false;
        }
    }
}
