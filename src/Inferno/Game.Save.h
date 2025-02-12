#pragma once

namespace Inferno {
    //void SaveGame(const filesystem::path& path);
    void SaveGame(string_view name, bool autosave);
}