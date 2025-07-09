#include <pch.h>
#include "Mods.h"
#include "Level.h"
#include "Resources.Common.h"
#include "Yaml.h"

namespace Inferno {
    bool ModManifest::SupportsLevel(const Level& level) const {
        if (level.IsDescent1() && SupportsD1()) return true;
        if (level.IsDescent2() && SupportsD2()) return true;
        return false;
    }

    ModManifest ReadModManifest(const string& yaml) try {
        ryml::Tree doc = ryml::parse_in_arena(ryml::to_csubstr(yaml));
        ryml::NodeRef root = doc.rootref();

        ModManifest manifest{};

        if (!root.is_map()) {
            SPDLOG_WARN("Manifest is empty");
            return manifest;
        }

        Yaml::ReadValue2(root, "name", manifest.name);
        Yaml::ReadValue2(root, "version", manifest.version);
        Yaml::ReadSequence(root, "supports", manifest.supports);
        Yaml::ReadValue2(root, "author", manifest.author);
        Yaml::ReadValue2(root, "description", manifest.description);
        return manifest;
    }
    catch (const std::exception& e) {
        SPDLOG_WARN("Error reading mod manifest: {}", e.what());
        return {};
    }

    Option<ModManifest> ReadModManifest(const IZipFile& zip) {
        if (auto bytes = zip.TryReadEntry(MOD_MANIFEST_FILE)) {
            return ReadModManifest(BytesToString(*bytes));
        }

        return {};
    }
}
