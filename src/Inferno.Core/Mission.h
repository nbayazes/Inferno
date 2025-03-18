#pragma once

#include <map>
#include <fstream>
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
            if (Metadata.contains(key))
                return Metadata.at(key);

            return "";
        }

        bool Read(std::istream& file) {
            if (!file) return false;

            string line;
            while (std::getline(file, line)) {
                if (String::Contains(line, "=")) {
                    // trim trailing comments (only applies to very old files)
                    if (auto idx = String::IndexOf(line, ";"))
                        line = line.substr(0, *idx);

                    string key, value;
                    if (auto idx = String::IndexOf(line, "=")) {
                        key = String::Trim(String::ToLower(line.substr(0, *idx)));
                        value = String::Trim(line.substr(*idx + 1));
                    }
                    else {
                        SPDLOG_WARN("Equals sign expected in mission value: {}", line);
                        continue;
                    }

                    using String::Hash;
                    switch (Hash(key)) {
                        case Hash("name"):
                            Name = value;
                            Enhancement = MissionEnhancement::Standard;
                            break;
                        case Hash("xname"):
                            Name = value;
                            Enhancement = MissionEnhancement::Ham;
                            break;
                        case Hash("zname"):
                            Name = value;
                            Enhancement = MissionEnhancement::VertigoHam;
                            break;
                        case Hash("type"):
                            Type = value == "anarchy" ? "anarchy" : "normal";
                            break;
                        case Hash("num_levels"):
                        {
                            auto count = std::stoi(value);
                            for (int i = 0; i < count; i++) {
                                std::getline(file, line);
                                line = String::Trim(line);
                                Levels.push_back(line);
                            }
                            break;
                        }
                        case Hash("num_secrets"):
                        {
                            auto count = std::stoi(value);
                            for (int i = 0; i < count; i++) {
                                std::getline(file, line);
                                line = String::Trim(line);
                                SecretLevels.push_back(line);
                            }
                            break;
                        }
                        default: Metadata[key] = value;
                            break;
                    }
                }
                else if (line.starts_with(";")) {
                    Comments.append(line.substr(1) + '\n');
                }
            }

            return true;
        }

        void Write(std::filesystem::path path) {
            if (Name == "") throw Exception("Mission name cannot be empty");
            if (Name.size() > MaxNameLength) throw Exception("Mission name must be under 26 characters");
            if (Levels.size() < 1) throw Exception("Mission must have at least one level");

            std::ofstream stream(path);
            if (!stream) throw Exception("Unable to open file to write mission");

            string nameProp = [&] {
                switch (Enhancement) {
                    default:
                    case MissionEnhancement::Standard: return "name";
                    case MissionEnhancement::Ham: return "xname";
                    case MissionEnhancement::VertigoHam: return "zname";
                }
            }();

            WriteProperty(stream, nameProp, Name);
            WriteProperty(stream, "type", Type);
            WriteProperty(stream, "num_levels", std::to_string(Levels.size()));

            for (auto& level : Levels)
                stream << level << '\n';

            if (SecretLevels.size() > 0) {
                WriteProperty(stream, "num_secrets", (int)SecretLevels.size());
                for (auto& level : SecretLevels)
                    stream << level << '\n';
            }


            for (const auto& [key, value] : Metadata) {
                if (value == "") continue;

                WriteProperty(stream, key, value);
            }

            {
                std::stringstream comments(Comments.data());
                string line;

                while (std::getline(comments, line, '\n'))
                    stream << ';' << line << '\n';
            }
        }

        // Returns the nearest secret level after the current level
        Option<int> FindSecretLevel(int currentLevelIndex) const {
            for (int i = 0; i < SecretLevels.size(); i++) {
                auto tokens = String::Split(SecretLevels[i], ',');

                if (tokens.size() == 2) {
                    auto index = std::stoi(tokens[1]);
                    if (index >= currentLevelIndex)
                        return -(i + 1); // secret levels are negative
                }
            }

            return {}; // Didn't find it
        }

    private:
        static void WriteProperty(std::ostream& stream, const string& name, const string& value) {
            string str = value; // trim nulls
            if (str == "") return;
            stream << name << " = " << str << '\n';
        }

        static void WriteProperty(std::ostream& stream, const string& name, bool value) {
            if (!value) return;
            stream << name << " = " << (value ? "yes" : "no") << '\n';
        }

        static void WriteProperty(std::ostream& stream, const string& name, int value) {
            stream << name << " = " << std::to_string(value) << '\n';
        }
    };
}
