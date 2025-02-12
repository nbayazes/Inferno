#include "pch.h"
#include "Game.Save.h"
#include "Game.h"
#include "Yaml.h"
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_time.h>

namespace Inferno {
    string FormatDate(const SDL_DateTime& dateTime) {
        string month = [&] {
            switch (dateTime.month) {
                default:
                case 0: return "Jan";
                case 1: return "Feb";
                case 2: return "Mar";
                case 3: return "Apr";
                case 4: return "May";
                case 5: return "Jun";
                case 6: return "Jul";
                case 7: return "Aug";
                case 8: return "Sep";
                case 9: return "Oct";
                case 10: return "Nov";
                case 11: return "Dec";
            }
        }();

        auto timeLabel = dateTime.hour > 11 ? "PM" : "AM";
        auto time = fmt::format("{}:{} {}", dateTime.hour > 12 ? dateTime.hour - 12 : dateTime.hour, dateTime.minute, timeLabel); // 11:00 AM
        auto date = fmt::format("{} {}, {}", month, dateTime.day, dateTime.year); // Jan 12, 2025
        return fmt::format("{} {}", date, time); 
    }

    void SaveGame(const filesystem::path& path) {
        try {
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

            auto mission = Game::GetMissionInfo();

            auto& player = Game::Player;
            doc["Date"] << FormatDate(dateTime);
            //doc["Time"] << fmt::format("{}:{}", dateTime.hour, dateTime.minute);
            doc["LevelNumber"] << Game::LevelNumber;
            doc["LevelName"] << Game::Level.Name;
            doc["MissionName"] << mission.Name;
            doc["MissionFile"] << mission.Path.string();

            doc["Shields"] << player.Shields;
            doc["Energy"] << player.Energy;
            doc["PrimaryWeapons"] << player.PrimaryWeapons;
            doc["SecondaryWeapons"] << player.SecondaryWeapons;
            Yaml::WriteSequence(doc["PrimaryAmmo"], player.PrimaryAmmo);
            Yaml::WriteSequence(doc["SecondaryAmmo"], player.SecondaryAmmo);
            doc["Primary"] << (int)player.Primary;
            doc["Secondary"] << (int)player.Secondary;
            doc["Powerups"] << ToUnderlying(player.Powerups);
            doc["BombIndex"] << player.BombIndex;
            doc["LaserLevel"] << player.LaserLevel;

            doc["Lives"] << player.Lives;
            doc["Score"] << player.Score;

            doc["TotalKills"] << player.Stats.TotalKills;
            doc["TotalTime"] << player.TotalTime;

            //auto statsNode = doc["Stats"];
            //statsNode |= ryml::MAP;

            //statsNode["TotalKills"] << player.Stats.TotalKills;
            //statsNode["TotalTime"] << player.TotalTime;
            //statsNode["Kills"] << player.Stats.Kills;

            filesystem::path temp = path;
            temp.replace_filename("temp.sav");

            {
                std::ofstream file(temp);
                file << doc;
            }

            // Write went okay, ovewrite the old file and remove temp
            filesystem::copy(temp, path, filesystem::copy_options::overwrite_existing);
            filesystem::remove(temp);
        }
        catch (const std::exception& e) {
            ShowErrorMessage("Error saving game");
            SPDLOG_ERROR("Error saving game:\n{}", e.what());
        }
    }

    void SaveGame(string_view name, bool autosave) {
        if (auto folder = SDL_GetUserFolder(SDL_FOLDER_SAVEDGAMES)) {
            auto saveFolder = filesystem::path(folder) / "Inferno";
            if (!filesystem::exists(saveFolder))
                filesystem::create_directory(saveFolder);

            SaveGame(saveFolder / name);
        }
        else {
            SPDLOG_ERROR("Unable to get user folder");
        }
    }
}
