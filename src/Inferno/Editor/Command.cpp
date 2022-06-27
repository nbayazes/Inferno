#include "pch.h"
#include "Command.h"
#include "Editor.Undo.h"

void Inferno::Editor::Command::Execute() const {
    try {
        if (!CanExecute()) return;
        assert(Action || SnapshotAction);
        if (SnapshotAction) {
            if (auto label = SnapshotAction(); !label.empty())
                Editor::History.SnapshotLevel(label);
        }
        else if (Action) {
            Action();
        }
    }
    catch (const std::exception& e) {
        ShowErrorMessage(e);
    }
}
