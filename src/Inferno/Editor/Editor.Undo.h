#pragma once

#include <algorithm>

#include "Types.h"
#include "Level.h"
#include "Events.h"
#include "Editor.Selection.h"

namespace Inferno::Editor {
    template<class...TArgs>
    void SetStatusMessage(string_view format, TArgs&&...args);

    class EditorHistory {
        size_t _currentId = 0, _cleanId = 0;

        struct Snapshot {
            size_t ID; // Unique identifier
            string Name; // Name to show in the UI
            std::function<void(Level&)> Apply; // Function to apply (restore) this snapshot state
            Tag Selection;
            MultiSelection Marked;

            enum Flag {
                Nothing = 0,
                Selections = BIT(0),
                Level = BIT(1)
            } Data;

            void RestoreSelection() const {
                // Directly set selection to prevent echoing select actions
                Editor::Selection.SetSelection(Selection);
                Editor::Marked = Marked;
            }

            void Restore(Inferno::Level* level) const {
                if (Apply && level) Apply(*level);
                Events::LevelChanged();
            }
        };

        Level* _level;
        std::list<Snapshot> _snapshots;
        std::list<Snapshot>::iterator _snapshot; // pointer to the current snapshot
        int _undoLevels;

    public:
        EditorHistory(Level* level, int undoLevels = 50) : _level(level), _undoLevels(undoLevels) {
            _undoLevels = std::max(_undoLevels, 10);
            Reset();
        }

        void UpdateCleanSnapshot() {
            if (auto snapshot = FindDataSnapshot())
                _cleanId = snapshot->ID;
            else
                _cleanId = (size_t)-1;

            Shell::UpdateWindowTitle();
        }

        void Reset() {
            _snapshots.clear();
            _snapshot = _snapshots.begin();
            if (_level != nullptr) {
                SnapshotLevel("Load Level");
                UpdateCleanSnapshot();
            }
        }

        // Snapshots the selection if it has changed
        void SnapshotSelection() {
            if (!_level) return;

            // Compare selection to snapshot and exit if nothing changed
            if (_snapshot != _snapshots.begin()) {
                if (_snapshot->Selection == Editor::Selection.Tag() &&
                    _snapshot->Marked == Marked)
                    return;
            }

            AddSnapshot("Selection", Snapshot::Selections, { });
        }

        // Snapshots everything
        void SnapshotLevel(string_view name) {
            if (!_level) return;

            // Copy the current level state into the lambda used to restore this snapshot
            AddSnapshot(name, Snapshot::Level, [copy = *_level](Level& level) {
                level = copy;
            });

            SetStatusMessage(name);
        }

        // Restores the current snapshot. Similar to undo, but doesn't modify the stack
        void Restore() {
            if (!CanUndo()) return;
            SetStatusMessage("Restoring {}", _snapshot->Name);
            _snapshot->Restore(_level);
            _snapshot->RestoreSelection();
        }

        bool CanUndo() {
            return _level && _snapshot != _snapshots.begin();
        }

        bool CanRedo() {
            return _level && _snapshot != _snapshots.end() && _snapshot != std::prev(_snapshots.end());
        }

        string GetUndoName() {
            if (!CanUndo()) return {};
            return _snapshot->Name;
        }

        string GetRedoName() {
            if (!CanRedo()) return {};
            return std::next(_snapshot)->Name;
        }

        void Undo() {
            if (!CanUndo()) return;
            SetStatusMessage("Undo: {}", _snapshot->Name);

            if (auto snapshot = FindPastDataSnapshot())
                snapshot->Restore(_level);

            // Snapshots can delete the current segment, try to find a valid selection
            for (std::list<Snapshot>::reverse_iterator snapshot(_snapshot); snapshot != _snapshots.rend(); snapshot++) {
                if (_level->SegmentExists(snapshot->Selection)) {
                    snapshot->RestoreSelection();
                    break;
                }
            }

            _snapshot--;
            Shell::UpdateWindowTitle();
            Events::SnapshotChanged();
        }

        void Redo() {
            if (!CanRedo()) return;
            _snapshot++;
            SetStatusMessage("Redo: {}", _snapshot->Name);
            _snapshot->Restore(_level);
            _snapshot->RestoreSelection();
            Shell::UpdateWindowTitle();
            Events::SnapshotChanged();
        }

        auto Snapshots() const { return _snapshots.size(); }

        bool Dirty() {
            if (_cleanId == -1) return true;

            if (auto snapshot = FindDataSnapshot())
                return _cleanId != snapshot->ID;

            return false;
        }

    private:
        // Scans backwards until a snapshot containing non-selection data is reached.
        // Does not include the current snapshot
        Snapshot* FindPastDataSnapshot() {
            if (_snapshots.empty()) return nullptr;

            for (std::list<Snapshot>::reverse_iterator snapshot(_snapshot); snapshot != _snapshots.rend(); snapshot++) {
                if (snapshot->Data != Snapshot::Selections) {
                    return &(*snapshot);
                }
            }

            return nullptr;
        }

        Snapshot* FindDataSnapshot() {
            if (_snapshots.empty()) return nullptr;

            if (_snapshot->Data != Snapshot::Selections)
                return &(*_snapshot);

            return FindPastDataSnapshot();
        }

        bool SnapshotHasData(Snapshot::Flag flag) {
            if (_snapshot == _snapshots.begin()) return false;
            return _snapshot->Data & flag;
        }

        void AddSnapshot(string_view name, Snapshot::Flag flag, const std::function<void(Level&)>&& apply) {
            //SPDLOG_INFO("Snapshotting {}", name);
            Snapshot snapshot{ _currentId++, string(name), apply, Editor::Selection.Tag(), Marked, flag };

            // discard redos if we're not at latest snapshot
            if (_snapshot != _snapshots.end())
                _snapshots.erase(std::next(_snapshot), _snapshots.end());

            _snapshots.push_back(std::move(snapshot));

            while (_snapshots.size() > _undoLevels)
                _snapshots.pop_front(); // respect the max undo levels

            _snapshot = _snapshots.end();
            _snapshot--;

            Shell::UpdateWindowTitle();
        }
    };

    inline EditorHistory History(nullptr);
}
