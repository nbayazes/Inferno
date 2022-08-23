#pragma once
#include "Types.h"
#include "Level.h"
#include "Command.h"
#include "HogFile.h"

namespace Inferno::Editor {
    // Creates a backup of a file using the provided extension
    void BackupFile(filesystem::path path, string_view ext = ".bak");

    void NewLevel(string name, string fileName, int16 version, bool addToHog);
    void CheckForAutosave();
    void ResetAutosaveTimer();
    void WritePlaytestLevel(filesystem::path missionFolder, Level& level, HogFile* mission);

    namespace Commands {
        extern Command ConvertToD2, ConvertToVertigo;
        extern Command NewLevel, Open, Save, SaveAs;
    }
}
