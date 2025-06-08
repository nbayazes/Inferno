#include "pch.h"
#include "Mission.h"
#include "spdlog/spdlog.h"
#include <fstream>

namespace Inferno {
    namespace {
        void WriteProperty(std::ostream& stream, const string& name, const string& value) {
            if (value.empty()) return;
            string val = value.c_str(); // NOLINT(readability-redundant-string-cstr) trim to null terminator
            stream << name << " = " << val << '\n';
        }

        void WriteProperty(std::ostream& stream, const string& name, bool value) {
            if (!value) return;
            stream << name << " = " << (value ? "yes" : "no") << '\n';
        }

        void WriteProperty(std::ostream& stream, const string& name, int value) {
            stream << name << " = " << std::to_string(value) << '\n';
        }
    }

    bool MissionInfo::Read(std::istream& file) {
        if (!file) return false;

        string line;
        while (std::getline(file, line)) {
            if (String::Contains(line, "=")) {
                // trim trailing comments (only applies to very old files)
                line = TrimComment(line);

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
                    case Hash("num_levels"): {
                        auto count = std::stoi(value);
                        for (int i = 0; i < count; i++) {
                            std::getline(file, line);
                            line = String::Trim(TrimComment(line));
                            Levels.push_back(line);
                        }
                        break;
                    }
                    case Hash("num_secrets"): {
                        auto count = std::stoi(value);
                        for (int i = 0; i < count; i++) {
                            std::getline(file, line);
                            line = String::Trim(TrimComment(line));
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

    void MissionInfo::Write(std::filesystem::path path) {
        Name = Name.c_str(); // NOLINT(readability-redundant-string-cstr) trim to null terminator

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

    Option<int> MissionInfo::FindSecretLevel(int currentLevelIndex) const {
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
}
