#pragma once
#include "Game.h"
#include "HogFile.h"
#include "WindowBase.h"
#include "Mission.h"
#include "WindowsDialogs.h"
#include "Editor/Editor.Undo.h"

namespace Inferno::Editor {
    class MissionEditor : public ModalWindowBase {
        struct MissionEntry {
            string File;
            bool IsSecret;
            int ID; // ID for ImGui
        };

        List<MissionEntry> _entries;
        int _selection = -1; // level list selection index
        int _entryId = 0;

        // todo: set enhancement based on contained HOG level versions
        MissionInfo _mission;

        // Copies a string to a character buffer, leaving one character for null
        //static void CopyToBuffer(const string& src, std::span<char> dest) {
        //    src.copy(dest.data(), std::min(src.size(), dest.size() - 1));
        //}

    public:
        MissionEditor() : ModalWindowBase("Mission Editor") {
            Width = 500 * Shell::DpiScale;
        };

    protected:
        bool OnOpen() override {
            if (auto info = Game::GetMissionInfo(*Game::Mission)) {
                _mission = *info;
            }

            _entries.clear();

            // if there's no levels in the mission, add all levels from the hog
            if (Game::Mission && _mission.Levels.empty()) {
                AddMissingLevels(*Game::Mission);
            }
            else {
                for (auto& l : _mission.Levels)
                    _entries.push_back({ l, false, _entryId++ });

                // insert secret levels at their correct index
                for (int i = 0; i < _mission.SecretLevels.size(); i++) {
                    auto tokens = String::Split(_mission.SecretLevels[i], ',');
                    if (tokens.size() == 2) {
                        auto index = std::stoi(tokens[1]) - 1;
                        _entries.insert(_entries.begin() + index + i, { tokens[0], true });
                    }
                }
            }

            _selection = _entries.size() > 0 ? 0 : -1;
            return true;
        }

        bool OnAccept() override {
            // write file
            try {
                // update mission based on selections
                _mission.Levels.clear();
                _mission.SecretLevels.clear();

                bool prevWasSecret = false;
                for (int i = 0; i < _entries.size(); i++) {
                    auto& entry = _entries[i];
                    if (entry.IsSecret) {
                        if (prevWasSecret) continue;
                        int index = i + 1 - (int)_mission.SecretLevels.size();
                        auto str = fmt::format("{},{}", entry.File, index);
                        _mission.SecretLevels.push_back(str);
                    }
                    else {
                        _mission.Levels.push_back(entry.File);
                    }
                }

                _mission.Write(Game::Mission->GetMissionPath());
            }
            catch (const std::exception& e) {
                ShowErrorMessage(e);
            }

            return true;
        }

        void MissionTab() {
            if (!ImGui::BeginTabItem("Mission")) return;

            ImGui::TextInputWide("Name", _mission.Name, MissionInfo::MaxNameLength);

            bool isSinglePlayer = _mission.Type == "normal";
            if (ImGui::RadioButton("Single player##type", isSinglePlayer))
                _mission.Type = "normal";

            if (_mission.Enhancement == MissionEnhancement::VertigoHam) {
                ImGui::SameLine();
                ImGui::TextColored({ 0.25f, 1, 0.25f, 1 }, "Vertigo Enhanced");
            }

            if (ImGui::RadioButton("Multiplayer##type", !isSinglePlayer))
                _mission.Type = "anarchy";


            if (Game::Mission && Game::Mission->IsDescent1()) {
                ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                ImGui::TextInputWide("Briefing TEX/TXB", _mission.Metadata["briefing"], 12); // 8.3 file name
                ImGui::TextInputWide("Ending TEX/TXB", _mission.Metadata["ending"], 12);  // 8.3 file name
            }

            {
                ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                ImGui::BeginChild("level list container", { -1, -1 }, false);

                {
                    ImGui::BeginChild("level list", { Width - 150 * Shell::DpiScale, -1 }, true);
                    for (int n = 0; n < _entries.size(); n++) {
                        auto& entry = _entries[n];
                        auto label = entry.IsSecret ? fmt::format("{} (secret)", entry.File) : entry.File;
                        ImGui::PushID(entry.ID);

                        if (ImGui::Selectable(label.c_str(), _selection == n))
                            _selection = n;

                        if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                            int nNext = n + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);
                            if (nNext >= 0 && nNext < _entries.size()) {
                                std::swap(_entries[n], _entries[nNext]);
                                _selection = nNext;
                                ImGui::ResetMouseDragDelta();
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }

                ImGui::SameLine();

                {
                    ImGui::BeginChild("level list btns", { -1, -1 });

                    constexpr ImVec2 btnSize = { -1, 0 };

                    {
                        DisableControls disable(_selection == -1 || _selection >= _entries.size());

                        if (ImGui::Button("Toggle Secret", btnSize)) {
                            _entries[_selection].IsSecret = !_entries[_selection].IsSecret;
                        }

                        if (ImGui::Button("Duplicate", btnSize)) {
                            MissionEntry entry = _entries[_selection];
                            entry.ID = ++_entryId;
                            _entries.insert(_entries.begin() + _selection, entry);
                            _selection++;
                        }

                        if (ImGui::Button("Remove", btnSize)) {
                            _entries.erase(_entries.begin() + _selection);
                            _selection--;
                        }
                    }

                    ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                    if (Game::Mission && ImGui::Button("Add Missing", btnSize))
                        AddMissingLevels(*Game::Mission);

                    ImGui::EndChild();
                }

                ImGui::EndChild();
            }

            ImGui::EndTabItem();
        }

        void MetadataCheckbox(const char* label, const string& key) {
            bool value = _mission.GetBool(key);
            if (ImGui::Checkbox(label, &value))
                _mission.SetBool(key, value);
        };

        void AuthorTab() {
            if (!ImGui::BeginTabItem("Author")) return;

            ImGui::TextInputWide("Author", _mission.Metadata["author"], 128);
            ImGui::TextInputWide("Editor", _mission.Metadata["editor"], 128);
            ImGui::TextInputWide("Build time", _mission.Metadata["build_time"], 128);
            ImGui::TextInputWide("Date", _mission.Metadata["date"], 128);
            ImGui::TextInputWide("Revision", _mission.Metadata["revision"], 128);
            ImGui::TextInputWide("Email", _mission.Metadata["email"], 128);
            ImGui::TextInputWide("Website", _mission.Metadata["web_site"], 128);

            ImGui::Text("Custom assets:");
            MetadataCheckbox("Textures", "custom_textures");
            MetadataCheckbox("Robots", "custom_robots");
            MetadataCheckbox("Music", "custom_music");

            ImGui::EndTabItem();
        }

        void MetadataTab() {
            if (!ImGui::BeginTabItem("Metadata")) return;

            ImGui::Text("Supported modes:");
            MetadataCheckbox("Single player", "normal");
            MetadataCheckbox("Cooperative", "coop");
            MetadataCheckbox("Anarchy", "anarchy");
            MetadataCheckbox("Robot anarchy", "robo_anarchy");
            MetadataCheckbox("Capture the flag", "capture_flag");
            MetadataCheckbox("Hoard", "hoard");

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Separator();
            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            MetadataCheckbox("Multi author", "multi_author");
            //ImGui::SameLine();
            //MetadataCheckbox("Want feedback", "want_feedback");

            if (_mission.Comments.capacity() < 2048)
                _mission.Comments.resize(2048);

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text("Comments:");
            ImGui::InputTextMultiline("##Comments", _mission.Comments.data(), 2048, { -1, -1 });
            ImGui::EndTabItem();
        }

        void OnUpdate() override {
            ImGui::BeginChild("prop_panel", { -1, 700 * Shell::DpiScale });

            if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None)) {
                MissionTab();
                AuthorTab();
                MetadataTab();
                ImGui::EndTabBar();
            }
            ImGui::EndChild();

            AcceptButtons();
        }

    private:
        void AddMissingLevels(const HogFile& mission) {
            auto filter = mission.IsDescent1() ? "rdl" : "rl2";
            auto levels = mission.GetContents(filter);

            for (auto& level : levels) {
                if (Seq::find(_entries, [&level](const MissionEntry& e) { return e.File == level; }))
                    continue;

                MissionEntry entry = { level, false };
                _entries.push_back(entry);
            }
        }
    };

    class RenameHogFileDialog : public ModalWindowBase {
    public:
        RenameHogFileDialog() : ModalWindowBase("Rename File") {};

        string Name;

    protected:
        void OnUpdate() override {
            char buffer[8 + 1 + 3 + 1]{};
            auto len = Name.copy(buffer, 8 + 1 + 3);
            buffer[len] = '\0';

            SetInitialFocus();
            if (ImGui::InputTextEx("##input", nullptr, buffer, (int)std::size(buffer), { -1, 0 }, 0))
                Name = FormatShortFileName(buffer);
            EndInitialFocus();

            AcceptButtons();
        }
    };

    class RenameLevelDialog : public ModalWindowBase {
    public:
        RenameLevelDialog() : ModalWindowBase("Rename Level") {}

        string LevelName;

    protected:
        bool OnOpen() override {
            LevelName = Game::Level.Name;
            return true;
        }

        bool OnAccept() override {
            Game::Level.Name = LevelName;
            Inferno::Editor::History.SnapshotLevel("Rename Level");
            return true;
        }

        void OnUpdate() override {
            char name[Level::MAX_NAME_LENGTH]{};
            LevelName.copy(name, std::min((int)LevelName.size(), Level::MAX_NAME_LENGTH));

            SetInitialFocus();
            if (ImGui::InputTextEx("##renamelevel", nullptr, name, Level::MAX_NAME_LENGTH, { -1, 0 }, 0))
                LevelName = string(name);
            EndInitialFocus();

            AcceptButtons();
        }
    };
}
