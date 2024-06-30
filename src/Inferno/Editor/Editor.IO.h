#pragma once
#include "Command.h"
#include "HogFile.h"
#include "Level.h"
#include "Types.h"

namespace Inferno::Editor {
    struct NewLevelInfo {
        string Title;
        string FileName;
        int16 Version = 0;
        bool AddToHog = false;
    };

    [[nodiscard]] Level CreateNewLevel(const NewLevelInfo& info);

    // Creates a backup of a file using the provided extension
    void BackupFile(const filesystem::path& path, string_view ext = ".bak");

    void CheckForAutosave();
    void ResetAutosaveTimer();
    void WritePlaytestLevel(const filesystem::path& missionFolder, Level& level, HogFile* mission);

    namespace Commands {
        extern Command ConvertToD2, ConvertToVertigo;
        extern Command NewLevel, Open, Save, SaveAs;
    }
}
