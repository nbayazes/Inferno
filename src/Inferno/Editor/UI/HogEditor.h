#pragma once
#include "WindowBase.h"
#include "HogFile.h"
#include "WindowsDialogs.h"
#include "Game.h"
#include "Editor/Editor.h"
#include "Editor/Editor.IO.h"
#include "FileSystem.h"

namespace Inferno::Editor {
    class HogEditor : public ModalWindowBase {
        RenameHogFileDialog _renameDialog;
        List<HogEntry> _entries;
        List<int> _selections;
        bool _onlyShowLevels = true;
        bool _skipDeleteConfirmation = false;

    public:
        HogEditor() : ModalWindowBase("HOG Editor") {
            Width = 500 * Shell::DpiScale;

            _renameDialog.Callback = [this](bool accepted) {
                if (!accepted) return;
                OnRename(_renameDialog.Name);
            };
        }

        void OnRename(string newName) try {
            auto entries = GetEntries();

            if (_selections.empty() || !Seq::inRange(entries, _selections[0]))
                return;

            filesystem::path tempPath = Game::Mission->Path;
            tempPath.replace_extension(".tmp");

            {
                HogReader reader(Game::Mission->Path);

                if (reader.TryFindEntry(newName)) {
                    ShowWarningMessage("File name is already in use", "Cannot rename");
                    _renameDialog.Show();
                    return;
                }

                HogEntry original = entries[_selections[0]];

                // Append the extension of the original name if one wasn't provided
                if (String::Extension(newName).empty())
                    newName += String::Extension(original.Name);

                HogWriter writer(tempPath);

                // rewrite all entries
                for (auto& entry : entries) {
                    //HogEntry local = entry;
                    auto data = reader.ReadEntry(entry.Name);
                    string name = entry.Name;

                    // when renaming a level also rename aux files (pog, hxm, ...)
                    if (original.IsLevel()) {
                        if (String::InvariantEquals(original.NameWithoutExtension(), entry.NameWithoutExtension()))
                            name = String::NameWithoutExtension(newName) + entry.Extension();
                    }
                    else if (String::InvariantEquals(original.Name, entry.Name)) {
                        name = newName;
                    }

                    if (name != entry.Name)
                        SPDLOG_INFO("Renaming {} to {}", entry.Name, name);

                    writer.WriteEntry(name, data);
                }

                // Replace the current level's file name if it matches
                if (Game::Level.FileName == original.Name) {
                    Game::Level.FileName = newName;
                    Shell::UpdateWindowTitle();
                }
            } // hog read/write scope

            ReplaceDestWithTemp(Game::Mission->Path, tempPath);
            LoadMission();
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            SPDLOG_ERROR(e.what());
        }

        void OnDelete() {
            if (!_skipDeleteConfirmation && !ShowYesNoMessage("Are you sure you want to delete the selected items?", "Confirm delete")) {
                return;
            }

            auto entries = GetEntries();

            Seq::sortDescending(_selections);
            for (auto& i : _selections) {
                //if (String::InvariantEquals(entries[i].Name, Game::Level.FileName)) {
                //    if (!ShowYesNoMessage("Are you sure you want to delete the currently opened level?", "Confirm delete"))
                //        continue;
                //}

                SPDLOG_INFO("Deleting entry {}", entries[i].Name);
                Seq::removeAt(entries, i);
            }

            _selections.clear();
            SaveChanges(*Game::Mission, entries);
        }

        static List<HogEntry> GetEntries() {
            HogReader reader(Game::Mission->Path);
            List<HogEntry> entries = { reader.Entries().begin(), reader.Entries().end() };
            Seq::sortBy(entries, [](const HogEntry& a, const HogEntry& b) { return a.Name < b.Name; });
            return entries;
        }

        void OnUpdate() override {
            const float panelHeight = 600 * Shell::DpiScale;

            ImGui::BeginChild("list", { 300 * Shell::DpiScale, panelHeight }, true);

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
            ImGui::BeginChild("buttons", { -1, panelHeight });

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
            //    }
            //}

            //{
            //    DisableControls disable(selection == -1 || !InRange(_entries, selection + 1));

            //    if (ImGui::Button("Move down", { -1, 0 })) {
            //        std::swap(_entries[selection + 1], _entries[selection]);
            //        _selections[0]++;
            //    }
            //}

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });

            {
                DisableControls disable(_selections.empty());
                if (ImGui::Button("Delete", { -1, 0 }))
                    OnDelete();
            }

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });

            ImGui::Checkbox("Skip confirmation", &_skipDeleteConfirmation);

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Skips the confirmation when deleting items");

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
            if (!Game::Mission) return;
            Game::LoadMission(Game::Mission->Path);
            _selections.clear();
            _entries = GetEntries();
        }

    private:
        void SaveChanges(const HogFile& source, span<HogEntry> entries) try {
            filesystem::path tempPath = source.Path;
            tempPath.replace_extension(".tmp");

            {
                HogReader reader(source.Path);
                HogWriter writer(tempPath);

                for (auto& entry : entries) {
                    auto data = reader.ReadEntry(entry.Name);
                    writer.WriteEntry(entry.Name, data);
                }
            } // hog read / write scope

            ReplaceDestWithTemp(source.Path, tempPath);
            LoadMission();
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
            SPDLOG_ERROR(e.what());
        }

        static void ReplaceDestWithTemp(const filesystem::path& dest, const filesystem::path& temp) {
            BackupFile(dest); // create backup of original
            filesystem::remove(dest); // Remove existing
            filesystem::rename(temp, dest); // Rename temp to destination
        }

        static void OpenLevel(const HogEntry& entry) {
            if (!entry.IsLevel()) return;
            if (!Editor::CanCloseCurrentFile()) return;
            Game::EditorLoadLevel(Game::Mission->Path, entry.Name);
        }

        int GetSelection() const {
            return _selections.empty() || _selections.size() > 1 ? -1 : _selections.front();
        }

        void OnImport() try {
            static const COMDLG_FILTERSPEC filter[] = {
                { L"Level", L"*.RL2;*.RDL" },
                { L"Robots", L"*.HXM" },
                { L"Textures", L"*.POG" },
                { L"Descent 1 Data", L"*.DTX" },
                { L"All Files", L"*.*" }
            };

            auto files = OpenMultipleFilesDialog(filter, "Import files to HOG");
            if (files.empty()) return;

            filesystem::path source = Game::Mission->Path;
            filesystem::path tempPath = source;
            tempPath.replace_extension(".tmp");

            {
                HogReader reader(source);
                HogWriter writer(tempPath);

                SPDLOG_INFO("Importing files to {}", Game::Mission->Path.string());

                // write the existing entries
                for (auto& entry : reader.Entries()) {
                    // check if the imported file matches an existing entry and update it
                    auto importPath = Seq::find(files, [&entry](const filesystem::path& path) {
                        auto shortName = FormatShortFileName(path.filename().string());
                        return String::InvariantEquals(entry.Name, shortName);
                    });

                    if (importPath) {
                        SPDLOG_INFO("Skipping existing file {} (will insert new copy later)", entry.Name);
                    }
                    else {
                        auto data = reader.ReadEntry(entry.Name);
                        writer.WriteEntry(entry.Name, data);
                    }
                }

                for (auto& file : files) {
                    //if (_entries.size() >= HogFile::MAX_ENTRIES)
                    //    throw Exception("HOG files can only contain 250 entries");

                    SPDLOG_INFO("Inserting file {}", file.string());
                    auto shortName = FormatShortFileName(file.filename().string());
                    auto data = File::ReadAllBytes(file);
                    writer.WriteEntry(shortName, data);
                }
            } // hog read / write scope

            ReplaceDestWithTemp(source, tempPath);
            LoadMission();
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
        }

        // Imports a robot or texture file to all levels in the mission
        void OnImportToLevels() {
            try {
                static constexpr COMDLG_FILTERSPEC filter[] = {
                    { L"Custom Data", L"*.HXM;*.POG;*.DTX" },
                    { L"Robots", L"*.HXM" },
                    { L"Textures", L"*.POG" },
                    { L"Music", L"*.HMP" },
                    { L"Descent 1 Data", L"*.DTX" },
                    { L"All Files", L"*.*" }
                };

                auto file = OpenFileDialog(filter, "Import file to levels");
                if (!file) return;

                auto size = filesystem::file_size(*file);

                List<HogEntry> newEntries;
                List<HogEntry> entries;
                int existingCount = 0;

                {
                    // load entries from disk
                    HogReader reader(Game::Mission->Path);
                    entries = { reader.Entries().begin(), reader.Entries().end() };
                }

                // Scan for new and existing entries
                for (auto& entry : entries) {
                    if (!entry.IsLevel()) continue;
                    auto name = entry.NameWithoutExtension() + file->extension().string();

                    if (FindEntry(name, entries)) {
                        existingCount++;
                    }
                    else {
                        HogEntry newEntry = { .Name = name, .Size = size, .Path = *file };
                        newEntries.push_back(newEntry);
                    }
                }

                if (existingCount) {
                    auto msg = fmt::format("{} existing files will be overwritten.", existingCount);
                    if (!ShowOkCancelMessage(msg, "Confirm Overwrite"))
                        return;
                }

                //if (_entries.size() + newEntries.size() > HogFile::MAX_ENTRIES)
                //    throw Exception("HOG files can only contain 250 entries");

                // Replace existing
                for (auto& entry : entries) {
                    if (!entry.IsLevel()) continue;
                    auto name = entry.NameWithoutExtension() + file->extension().string();

                    if (auto existing = FindEntry(name, entries)) {
                        existing->Path = *file;
                        existing->Size = size;
                    }
                }

                // Insert new
                for (auto& entry : newEntries)
                    entries.push_back(entry);

                SaveChanges(*Game::Mission, entries);
                LoadMission();
            }
            catch (const std::exception& e) {
                ShowErrorMessage(e);
            }
        }

        // Exports an entry to a destination
        static void ExportEntry(const HogEntry& entry, const filesystem::path& dest) {
            if (!Game::Mission) throw Exception("No hog is loaded");

            HogReader reader(Game::Mission->Path);
            auto data = reader.TryReadEntry(entry.Name);
            if (!data) throw Exception("Entry does not exist");

            std::ofstream file(dest, std::ios::binary);
            file.write((char*)data->data(), data->size());
        }

        void ExportFiles() try {
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

        void ExportFile() try {
            if (_selections.empty()) return;
            auto entry = Seq::tryItem(_entries, _selections[0]);
            if (!entry) return;

            static constexpr COMDLG_FILTERSPEC filter[] = {
                { L"All Files", L"*.*" }
            };

            if (auto path = SaveFileDialog(filter, 1, entry->Name, "Export File")) {
                ExportEntry(*entry, *path);
            }
        }
        catch (const std::exception& e) {
            SetStatusMessage("Error exporting file: {}", e.what());
        }

        static HogEntry* FindEntry(string_view name, span<HogEntry> entries) {
            return Seq::find(entries, [&name](const HogEntry& e) {
                return String::InvariantEquals(e.Name, name);
            });
        }
    };
}
