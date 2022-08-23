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
            Width = 500 * Shell::DpiScale;

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

                SaveChanges(*Game::Mission);
            };
        };

        void OnUpdate() override {
            const float PanelHeight = 600 * Shell::DpiScale;

            ImGui::BeginChild("list", { 300 * Shell::DpiScale, PanelHeight }, true);

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

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });

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

            if (ImGui::Button("Import", { -1, 0 }))
                OnImport();

            if (ImGui::Button("Import for\neach level", { -1, 0 }))
                OnImportToLevels();

            if (ImGui::IsItemHovered()) 
                ImGui::SetTooltip(
                    "Inserts a copy of a file for each level in the HOG. \n"
                    "It renames each copy to match the level file name.\n\n"
                    "This is intended for uniformly updating custom textures\n"
                    "and robots across all levels in a mission.");

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });

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

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });

            {
                DisableControls disable(!entry);
                if (ImGui::Button("Delete", { -1, 0 })) {
                    Seq::sortDescending(_selections);
                    for (auto& i : _selections)
                        Seq::removeAt(_entries, i);

                    _selections.clear();
                    _dirty = true;
                    SaveChanges(*Game::Mission);
                }
            }

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });

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
        void SaveChanges(HogFile& source) {
            filesystem::path tempPath = source.Path;
            tempPath.replace_extension(".tmp");

            try {
                HogWriter writer(tempPath);

                for (auto& entry : _entries) {
                    auto data = source.ReadEntry(entry);
                    writer.WriteEntry(entry.Name, data);
                }
            }
            catch (const std::exception& e) {
                ShowErrorMessage(e);
                SPDLOG_ERROR(e.what());
                return;
            }

            BackupFile(source.Path);
            filesystem::remove(source.Path); // Remove existing
            filesystem::rename(tempPath, source.Path); // Rename temp to destination

            Game::LoadMission(source.Path);
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
                    { L"Descent 1 Data", L"*.DTX" },
                    { L"All Files", L"*.*" }
                };

                auto files = OpenMultipleFilesDialog(filter, L"Import files to HOG");
                if (files.empty()) return;

                _selections.clear();
                _dirty = true;

                for (auto& file : files) {
                    if (_entries.size() >= HogFile::MAX_ENTRIES)
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
                SaveChanges(*Game::Mission);
            }
            catch (const std::exception& e) {
                ShowErrorMessage(e);
            }
        }

        // Imports a robot or texture file to all levels in the mission
        void OnImportToLevels() {
            try {
                static const COMDLG_FILTERSPEC filter[] = {
                    { L"Custom Data", L"*.HXM;*.POG;*.DTX" },
                    { L"Robots", L"*.HXM" },
                    { L"Textures", L"*.POG" },
                    { L"Music", L"*.HMP" },
                    { L"Descent 1 Data", L"*.DTX" },
                    { L"All Files", L"*.*" }
                };

                auto file = OpenFileDialog(filter, L"Import file to levels");
                if (!file) return;

                auto size = filesystem::file_size(*file);

                List<HogEntry> newEntries;
                int existingCount = 0;

                // Scan for new and existing entries
                for (auto& entry : _entries) {
                    if (!entry.IsLevel()) continue;
                    auto name = entry.NameWithoutExtension() + file->extension().string();

                    if (FindEntry(name)) {
                        existingCount++;
                    }
                    else {
                        HogEntry newEntry = { .Name = name, .Size = size, .Path = *file };
                        newEntries.push_back(newEntry);
                    }
                }

                if (existingCount) {
                    auto msg = fmt::format(L"{} existing files will be overwritten.", existingCount);
                    if (!ShowOkCancelMessage(msg, L"Confirm Overwrite"))
                        return;
                }

                if (_entries.size() + newEntries.size() > HogFile::MAX_ENTRIES)
                    throw Exception("HOG files can only contain 250 entries");

                _selections.clear();
                _dirty = true;

                // Replace existing
                for (auto& entry : _entries) {
                    if (!entry.IsLevel()) continue;
                    auto name = entry.NameWithoutExtension() + file->extension().string();

                    if (auto existing = FindEntry(name)) {
                        existing->Path = *file;
                        existing->Size = size;
                    }
                }

                // Insert new
                for (auto& entry : newEntries)
                    _entries.push_back(entry);

                SortEntries();
                SaveChanges(*Game::Mission);
            }
            catch (const std::exception& e) {
                ShowErrorMessage(e);
            }
        }

        // Exports an entry to a destination
        void ExportEntry(HogEntry& entry, filesystem::path dest) {
            auto data = Game::Mission->ReadEntry(entry);
            if (data.empty()) throw Exception("Entry does not exist");

            std::ofstream file(dest, std::ios::binary);
            file.write((char*)data.data(), data.size());
        }

        void ExportFiles() {
            try {
                auto path = BrowseFolderDialog();
                if (!path) return;

                for (auto& index : _selections) {
                    auto entry = Seq::tryItem(_entries, index);
                    if (!entry) continue;
                    ExportEntry(*entry, *path / entry->Name);
                }
            }
            catch (const std::exception& e) {
                SetStatusMessage("Error exporting files: {}", e.what());
            }
        }

        void ExportFile() {
            try {
                auto entry = Seq::tryItem(_entries, _selections[0]);
                if (!entry) return;

                static const COMDLG_FILTERSPEC filter[] = {
                    { L"All Files", L"*.*" }
                };

                if (auto path = SaveFileDialog(filter, 1, Convert::ToWideString(entry->Name), L"Export File")) {
                    ExportEntry(*entry, *path);
                }
            }
            catch (const std::exception& e) {
                SetStatusMessage("Error exporting file: {}", e.what());
            }
        }

        HogEntry* FindEntry(string_view name) {
            return Seq::find(_entries, [&name](HogEntry& e) {
                return String::InvariantEquals(e.Name, name);
            });
        }
    };

}