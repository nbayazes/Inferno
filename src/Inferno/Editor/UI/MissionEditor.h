#pragma once
#include "WindowBase.h"
#include "Mission.h"
#include "WindowsDialogs.h"

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
        MissionInfo Mission;
    public:
        MissionEditor() : ModalWindowBase("Mission Editor") {
            Width = 500 * Shell::DpiScale;
        };

    protected:
        bool OnOpen() override {
            auto mission = Game::TryReadMissionInfo();
            Mission = mission ? *mission : MissionInfo{};

            _entries.clear();

            // if there's no levels in the mission, add all levels from the hog
            if (Game::Mission && Mission.Levels.empty()) {
                AddMissingLevels(*Game::Mission);
            }
            else {
                for (auto& l : Mission.Levels)
                    _entries.push_back({ l, false, _entryId++ });

                // insert secret levels at their correct index
                for (int i = 0; i < Mission.SecretLevels.size(); i++) {
                    auto tokens = String::Split(Mission.SecretLevels[i], ',');
                    if (tokens.size() == 2) {
                        auto index = std::stoi(tokens[1]) - 1;
                        _entries.insert(_entries.begin() + index + i, { tokens[0], true });
                    }
                }
            }

            _selection = _entries.size() > 0 ? 0 : -1;
            return true;
        }

        void OnAccept() override {
            // write file
            try {
                // update mission based on selections
                Mission.Levels.clear();
                Mission.SecretLevels.clear();

                bool prevWasSecret = false;
                for (int i = 0; i < _entries.size(); i++) {
                    auto& entry = _entries[i];
                    if (entry.IsSecret) {
                        if (prevWasSecret) continue;
                        int index = i + 1 - (int)Mission.SecretLevels.size();
                        auto str = fmt::format("{},{}", entry.File, index);
                        Mission.SecretLevels.push_back(str);
                    }
                    else {
                        Mission.Levels.push_back(entry.File);
                    }
                }

                Mission.Write(Game::Mission->GetMissionPath());
            }
            catch (const std::exception& e) {
                ShowErrorMessage(e);
            }
        }

        void MissionTab() {
            if (!ImGui::BeginTabItem("Mission")) return;

            ImGui::TextInputWide("Name", Mission.Name, MissionInfo::MaxNameLength);

            bool isSinglePlayer = Mission.Type == "normal";
            if (ImGui::RadioButton("Single player##type", isSinglePlayer))
                Mission.Type = "normal";

            if (Mission.Enhancement == MissionEnhancement::VertigoHam) {
                ImGui::SameLine();
                ImGui::TextColored({ 0.25f, 1, 0.25f, 1 }, "Vertigo Enhanced");
            }

            if (ImGui::RadioButton("Multiplayer##type", !isSinglePlayer))
                Mission.Type = "anarchy";


            if (Game::Mission && Game::Mission->IsDescent1()) {
                ImGui::Dummy({ 0, 10 * Shell::DpiScale });
                ImGui::TextInputWide("Briefing TXB", Mission.Metadata["briefing"], 12);
                ImGui::TextInputWide("Ending TXB", Mission.Metadata["ending"], 12);
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
                            int n_next = n + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);
                            if (n_next >= 0 && n_next < _entries.size()) {
                                std::swap(_entries[n], _entries[n_next]);
                                _selection = n_next;
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

                    const ImVec2 btnSize = { -1, 0 };

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

        void MetadataCheckbox(const char* label, string key) {
            bool value = Mission.GetBool(key);
            if (ImGui::Checkbox(label, &value))
                Mission.SetBool(key, value);
        };

        void AuthorTab() {
            if (!ImGui::BeginTabItem("Author")) return;

            ImGui::TextInputWide("Author", Mission.Metadata["author"], 128);
            ImGui::TextInputWide("Editor", Mission.Metadata["editor"], 128);
            ImGui::TextInputWide("Build time", Mission.Metadata["build_time"], 128);
            ImGui::TextInputWide("Date", Mission.Metadata["date"], 128);
            ImGui::TextInputWide("Revision", Mission.Metadata["revision"], 128);
            ImGui::TextInputWide("Email", Mission.Metadata["email"], 128);
            ImGui::TextInputWide("Website", Mission.Metadata["web_site"], 128);

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

            if (Mission.Comments.capacity() < 2048)
                Mission.Comments.resize(2048);

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Text("Comments:");
            ImGui::InputTextMultiline("##Comments", Mission.Comments.data(), 2048, { -1, -1 });
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
                if (Seq::find(_entries, [&level](MissionEntry& e) { return e.File == level; }))
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
        RenameLevelDialog() : ModalWindowBase("Rename Level") {};

        string LevelName;
    protected:
        bool OnOpen() override {
            LevelName = Game::Level.Name;
            return true;
        }

        void OnAccept() override {
            Game::Level.Name = LevelName;
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
