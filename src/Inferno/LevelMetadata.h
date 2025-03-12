#pragma once

#include "Level.h"
#include "Settings.h"

namespace Inferno {
    void LoadLevelMetadata(Level& level, const string& data, LightSettings& lightSettings);
    void SaveLevelMetadata(const Level&, std::ostream&, const LightSettings& lightSettings);
    void SaveLevelMetadata(const Level&, const std::filesystem::path& path, const LightSettings& lightSettings);
}
