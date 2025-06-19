#pragma once

namespace Inferno {
    // Replaces game data with info from a yaml file
    void LoadGameTable(const string& yaml, FullGameData& gameData);
}
