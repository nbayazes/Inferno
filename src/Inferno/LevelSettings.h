#pragma once

#include "Types.h"
#include "Settings.h"
#include "Level.h"

namespace Inferno {
    void LoadLevelMetadata(Level& level, const string& data);
    void SaveLevelMetadata(const Level&, std::ostream&);  
}
