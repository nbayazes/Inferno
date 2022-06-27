#pragma once
#include "WindowBase.h"
#include "HogFile.h"
#include "WindowsDialogs.h"
#include "Convert.h"

namespace Inferno::Editor {

    class HogEditor : public ModalWindowBase {
        RenameHogFileDialog _renameDialog;
        List<HogEntry> _entries;
        List<int> _selections;
        bool _onlyShowLevels = true;
        bool _dirty = false;

    public:
        HogEditor() : ModalWindowBase("HOG Editor") {
            Width = 500;

            _renameDialog.Callback = [this](bool accepted) {
                if (!accepted) return;

                auto& renamed = _entries[_selections[0]];
                if (renamed.Name == _renameDialog.Name) return; // name didn't change

                if (Seq::find(_entries, [this, &renamed](auto x) { return x.Name == _renameDialog.Name; })) {
                    ShowWarningMessage(L"File name is already in use");
                    _renameDialog.Show();
                    return;
                }

                string newName = _renameDialog.Name;
                if (String::Extension(newName).empty())
                    newName += renamed.Extension();

                // Replace the current level's path if it matches
                if (Game::Level.FileName == renamed.Name) {
                    Game::Level.FileName = newName;
                    Editor::UpdateWindowTitle();
                }

                string originalName = renamed.NameWithoutExtension();
                renamed.Name = newName;
                _dirty = true;

                // when renaming a level also rename aux files (pog, hxm, ...)
                if (renamed.IsLevel()) {
                    for (auto& entry : _entries) {
                        if (String::InvariantEquals(originalName, entry.NameWithoutExtension())) {
                            entry.Name = renamed.NameWithoutExtension() + entry.Extension();
                        }
                    }
                }

                SaveChanges();
            };
        };

        void OnUpdate() override {
            constexpr float PanelHeight = 600;

            ImGui::BeginChild("list", { 280, PanelHeight }, true);

            for (int i = 0; i < _entries.size(); i++) {
                auto& entry = _entries[i];
                if (_onlyShowLevels && !entry.IsLevel()) continue;

                ImGui::PushID(i);

                auto name = entry.IsImport() ? entry.Name + " (import)" : entry.Name;
                bool selected = Seq::contains(_selections, i);

                if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::GetIO().KeyShift) {
                        if (_selections.empty()) {
                            _selections.push_back(i);
                        }
                        else {
                            int beg = _selections[0];
                            int end = i;
                            if (beg > end) std::swap(beg, end);

                            _selections.clear();

                            // select items between first selection and clicked item
                            for (int n = beg; n <= end; n++)
                                _selections.push_back(n);
                        }
                    }
                    else if (ImGui::GetIO().KeyCtrl) {
                        if (selected) Seq::remove(_selections, i);
                        else _selections.push_back(i);
                    }
                    else {
                        _selections.clear();
                        _selections.push_back(i);
                    }

                    if (ImGui::IsMouseDoubleClicked(0))
                        OpenLevel(entry);
                }

                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("buttons", { -1, PanelHeight });

            HogEntry* entry = _selections.size() == 1 ? &_entries[_selections[0]] : nullptr;
            {
                DisableControls disable(!entry || !entry->IsLevel() || entry->IsImport());
                if (ImGui::Button("Open", { -1, 0 }))
                    OpenLevel(*entry);
            }

            {
                DisableControls disable(!entry);
                if (ImGui::Button("Rename", { -1, 0 }) && entry) {
                    _renameDialog.Name = entry->Name;
                    _renameDialog.Show();
                }
            }

            ImGui::Dummy({ 0, 10 });

            {
                int count = 0;
                for (auto& i : _selections) {
                    if (auto item = Seq::tryItem(_entries, i); item && !item->IsImport())
                        count++;
                }

                DisableControls disable(count == 0);
                if (ImGui::Button("Export", { -1, 0 })) {
                    if (count > 1)
                        ExportFiles();
                    else
                        ExportFile();
                }
            }

            if (ImGui::Button("Import", { -1, 0 })) {
                OnImport();
            }

            ImGui::Dummy({ 0, 10 });

            //int selection = GetSelection();

            //{
            //    DisableControls disable(selection == -1 || !InRange(_entries, selection - 1));

            //    if (ImGui::Button("Move up", { -1, 0 })) {
            //        std::swap(_entries[selection - 1], _entries[selection]);
            //        _selections[0]--;
            //        _dirty = true;
            //    }
            //}

            //{
            //    DisableControls disable(selection == -1 || !InRange(_entries, selection + 1));

            //    if (ImGui::Button("Move down", { -1, 0 })) {
            //        std::swap(_entries[selection + 1], _entries[selection]);
            //        _selections[0]++;
            //        _dirty = true;
            //    }
            //}

            ImGui::Dummy({ 0, 10 });

            {
                DisableControls disable(!entry);
                if (ImGui::Button("Delete", { -1, 0 })) {
                    Seq::sortDescending(_selections);
                    for (auto& i : _selections)
                        Seq::removeAt(_entries, i);

                    _selections.clear();
                    _dirty = true;
                    SaveChanges();
                }
            }

            ImGui::Dummy({ 0, 10 });

            if (ImGui::Checkbox("Only show levels", &_onlyShowLevels))
                _selections.clear();

            if (entry) {
                ImGui::Text("Size: %i", entry->Size);
                ImGui::Text("Offset: %i", entry->Offset);
            }

            ImGui::EndChild();

            CloseButton("Close");

            _renameDialog.Update();
        }


    protected:
        bool OnOpen() override {
            if (!Game::Mission) return false;

            LoadMission();

            for (int i = 0; i < _entries.size(); i++) {
                if (_entries[i].Name == Game::Level.FileName) {
                    _selections.push_back(i);
                    break;
                }
            }

            return true;
        }

        void LoadMission() {
            _entries.clear();
            _selections.clear();
            _dirty = false;
            _entries = Game::Mission->Entries;
            SortEntries();
        }

    private:
        void SaveChanges() {
            Game::Mission->Save(_entries);
            Game::ReloadMission();
            LoadMission();
        }

        void OpenLevel(const HogEntry& entry) {
            if (!entry.IsLevel()) return;
            if (!Editor::CanCloseCurrentFile()) return;
            Editor::LoadLevelFromHOG(entry.Name);
        }

        int GetSelection() {
            return _selections.empty() || _selections.size() > 1 ? -1 : _selections.front();
        }

        void SortEntries() {
            Seq::sortBy(_entries, [](HogEntry& a, HogEntry& b) { return a.Name < b.Name; });
        }

        void OnImport() {
            try {
                static const COMDLG_FILTERSPEC filter[] = {
                    { L"Level", L"*.RL2;*.RDL" },
                    { L"Robots", L"*.HXM" },
                    { L"Textures", L"*.POG" },
                    { L"All Files", L"*.*" }
                };

                auto files = OpenMultipleFilesDialog(filter, L"Import files to HOG");
                if (files.empty()) return;

                _selections.clear();
                _dirty = true;

                for (auto& file : files) {
                    if (_entries.size() + 1 > HogFile::MAX_ENTRIES)
                        throw Exception("HOG files can only contain 250 entries");

                    auto size = filesystem::file_size(file);
                    auto shortName = FormatShortFileName(file.filename().string());

                    if (auto existing = FindEntry(shortName)) {
                        // Replace duplicates
                        existing->Path = file;
                        existing->Size = size;
                    }
                    else {
                        HogEntry entry = { .Name = shortName, .Size = size, .Path = file };
                        _entries.push_back(entry);
                    }
                }

                SortEntries();
                SaveChanges();
            }
            catch (const std::exception& e) {
                SetStatusMessage("Error adding file: {}", e.what());
            }
        }

        void ExportFiles() {
            try {
                auto path = BrowseFolderDialog();
                if (!path) return;

                for (auto& i : _selections) {
                    auto entry = Seq::tryItem(_entries, i);
                    if (!entry) continue;

                    Game::Mission->Export(i, *path / entry->Name);
                }
            }
            catch (const std::exception& e) {
                SetStatusMessage("Error exporting files: {}", e.what());
            }
        }

        void ExportFile() {
            try {
                auto selection = Seq::tryItem(_entries, _selections[0]);
                if (!selection) return;

                static const COMDLG_FILTERSPEC filter[] = {
                    { L"All Files", L"*.*" }
                };

                if (auto path = SaveFileDialog(filter, 1, Convert::ToWideString(selection->Name), L"Export File")) {
                    if (selection->Index)
                        Game::Mission->Export(selection->Index.value(), *path);
                }
            }
            catch (const std::exception& e) {
                SetStatusMessage("Error exporting file: {}", e.what());
            }
        }

        HogEntry* FindEntry(string_view name) {
            return Seq::find(_entries, [&name](HogEntry& e) {
                return e.Name == name;
            });
        }
    };

}