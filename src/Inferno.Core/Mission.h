#pragma once

#include <map>
#include <string>
#include "Types.h"
#include "Utility.h"

namespace Inferno {
    enum class MissionEnhancement {
        Standard, // Descent 1 or 2 mission
        Ham, // Descent 2 mission with v1.1 HAM. Unused.
        VertigoHam, // Descent 2 mission with v1.2 V-HAM
    };

    // Mission file describing the level order in a HOG (.MSN / .MN2).
    struct MissionInfo {
        static constexpr auto MaxNameLength = 25;
        string Name;
        string Type = "normal";
        MissionEnhancement Enhancement = MissionEnhancement::Standard;

        List<string> Levels;
        List<string> SecretLevels;
        string Comments;
        // Extra data not used by the game at runtime
        std::map<string, string> Metadata;
        std::filesystem::path Path; // File the mission info was loaded from

        List<string> GetSecretLevelsWithoutNumber() const {
            List<string> levels;

            for (auto& level : SecretLevels) {
                auto tokens = String::Split(level, ',');
                if (!tokens.empty())
                    levels.push_back(tokens[0]);
            }

            return levels;
        }

        void SetBool(const string& key, bool value) {
            Metadata[key] = value ? "yes" : "no";
        }

        bool GetBool(const string& key) const {
            if (Metadata.contains(key))
                return Metadata.at(key) == "yes";

            return false;
        }

        string GetValue(const string& key) const {
            if (Metadata.contains(key)) {
                auto& value = Metadata.at(key);
                return String::TrimEnd(value, "\0");
            }

            return "";
        }

        bool Read(std::istream& file);

        void Write(std::filesystem::path path);

        // Returns the nearest secret level after the current level
        Option<int> FindSecretLevel(int currentLevelIndex) const;

    private:
        // Removes a trailing comment
        static string TrimComment(string line) {
            if (auto idx = String::IndexOf(line, ";"))
                return line.substr(0, *idx);

            return line;
        }
    };
}
