#pragma once

#include "Level.h"
#include "Settings.h"

namespace Inferno {
    void LoadLevelMetadata(Level& level, const string& data, LightSettings& lightSettings);
    void SaveLevelMetadata(const Level& level, std::ostream& stream, LightSettings& lightSettings);
}
