#pragma once

#include "FileSystem.h"
#include "Utility.h"

namespace Inferno {
    struct Level;

    struct ModManifest {
        string name;
        string version;
        std::vector<string> supports;
        string author;
        string description;

        bool SupportsLevel(const Level& level) const;

    private:
        bool SupportsD1() const {
            for (auto& item : supports) {
                auto s = String::ToLower(item);
                if (s == "descent1" || s == "descent 1" || s == "d1")
                    return true;
            }

            return false;
        }

        bool SupportsD2() const {
            for (auto& item : supports) {
                auto s = String::ToLower(item);
                if (s == "descent2" || s == "descent 2" || s == "d2")
                    return true;
            }

            return false;
        }

    };

    ModManifest ReadModManifest(const string& yaml);
    Option<ModManifest> ReadModManifest(const IZipFile& zip);
}
