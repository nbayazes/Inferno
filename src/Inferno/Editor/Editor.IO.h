#pragma once
#include "Types.h"
#include "Level.h"
#include "Command.h"

namespace Inferno::Editor {
    std::vector<ubyte> WriteLevelToMemory(Level& level);
    void NewLevel(string name, string fileName, int16 version, bool addToHog);
    void CheckForAutosave();
    void ResetAutosaveTimer();
    void EnsureVertigoData(filesystem::path missionPath);

    namespace Commands {
        extern Command ConvertToD2, ConvertToVertigo;
        extern Command NewLevel, Open, Save, SaveAs;
    }
}
