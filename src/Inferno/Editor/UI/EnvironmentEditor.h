#pragma once
#include "WindowBase.h"
#include "Editor/Editor.Selection.h"
#include "Game.Segment.h"

namespace Inferno::Render {
    extern bool RebuildFogFlag;
}

namespace Inferno::Editor {
    class RenameEnvironmentDialog : public ModalWindowBase {
        friend class EnvironmentEditor;
        inline static Environment* _environment = nullptr;
        string _name;

    public:
        RenameEnvironmentDialog() : ModalWindowBase("Environment Name") {};

    protected:
        bool OnOpen() override {
            ASSERT(_environment); // must set environment before opening
            if (!_environment) return false;

            _name = _environment->name;
            return true;
        }

        bool OnAccept() override {
            if (_environment->name != _name) {
                if (Seq::exists(Game::Level.Environments, [this](const Environment& e) { return e.name == _name; })) {
                    ShowOkMessage("Environment name is already in use.", "Inferno Editor");
                    return false;
                }
            }

            if (_name.empty()) {
                ShowOkMessage("Name cannot be empty.", "Inferno Editor");
                return false;
            }

            // todo: update trigger references
            _environment->name = _name;

            Inferno::Editor::History.SnapshotLevel("Rename Environment");
            return true;
        }

        void OnUpdate() override {
            SetInitialFocus();
            ImGui::TextInputWide<64>("Environment name", _name);
            EndInitialFocus();

            AcceptButtons();
        }
    };

    class EnvironmentEditor final : public WindowBase {
        uint _index = 0;

    public:
        EnvironmentEditor() : WindowBase("Environment", &Settings::Editor.Windows.Environment) {}

        void OnUpdate() override {
            float contentWidth = ImGui::GetWindowContentRegionMax().x;
            auto& level = Game::Level;
            bool snapshot = false;

            auto environment = Game::GetEnvironment((EnvironmentID)_index);

            {
                ImVec2 btnSize = { 100 * Shell::DpiScale, 0 };
                ImGui::BeginChild("##available", ImVec2(-1, 200 * Shell::DpiScale), true);
                for (uint i = 0; i < level.Environments.size(); i++) {
                    if (ImGui::Selectable(level.Environments[i].name.c_str(), _index == i)) {
                        _index = i;
                        environment = Game::GetEnvironment((EnvironmentID)_index);

                        Editor::Marked.Clear();
                        Seq::insert(Editor::Marked.Segments, environment->segments);
                        Editor::History.SnapshotSelection();
                    }
                }

                ImGui::EndChild();

                if (ImGui::Button("Add", btnSize)) {
                    level.Environments.push_back({ .name = "new environment" });
                    _index = (uint)level.Environments.size() - 1;
                    environment = Game::GetEnvironment((EnvironmentID)_index);
                    environment->name = "new environment";
                    //_environment->damageSound = "ShieldHit";
                    //_environment->ambientSound = "CaveDrips";

                    auto segids = Editor::GetSelectedSegments();
                    SetEnvironmentSegments(Game::Level, *environment, segids);
                    snapshot = true;

                    RenameEnvironmentDialog::_environment = environment;
                    Events::ShowDialog(DialogType::RenameEnvironment);
                }

                if (ImGui::GetCursorPosX() + btnSize.x * 2 + 5 < contentWidth)
                    ImGui::SameLine();

                if (ImGui::Button("Rename", btnSize)) {
                    RenameEnvironmentDialog::_environment = environment;
                    Events::ShowDialog(DialogType::RenameEnvironment);
                }

                if (ImGui::GetCursorPosX() + btnSize.x * 3 + 5 < contentWidth)
                    ImGui::SameLine(0, 10);

                {
                    DisableControls disable(!environment);

                    if (ImGui::Button("Remove", btnSize)) {
                        auto msg = fmt::format(R"(Are you sure you want to remove environment '{}'?)", environment->name);
                        if (ShowYesNoMessage(msg, "Inferno Editor")) {
                            Seq::removeAt(level.Environments, _index);
                            environment = nullptr;
                            _index = std::clamp(_index, 0u, (uint)level.Environments.size());
                            snapshot = true;
                            Events::LevelChanged();
                        }
                    }
                }

                if (ImGui::Button("Update fog mesh")) {
                    Render::UpdateFogFlag = true;
                    Events::LevelChanged();
                }
            }

            ImGui::Dummy({ 0, 10 });

            constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable;

            if (!environment) {
                ImGui::Text("No environment");
                return;
            }

            if (ImGui::BeginTable("environment", 2, flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                {
                    //ImVec2 btnSize = { 75 * Shell::DpiScale, 0 };

                    ImGui::TableRowLabel("Segments");


                    if (ImGui::Button("Set segments")) {
                        auto segids = Editor::GetSelectedSegments();
                        SetEnvironmentSegments(Game::Level, *environment, segids);
                        snapshot = true;
                        Events::LevelChanged();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button("Add segments")) {
                        auto segids = Editor::GetSelectedSegments();
                        AddEnvironmentSegments(Game::Level, *environment, segids);
                        snapshot = true;
                        Events::LevelChanged();
                    }
                }

                ImGui::TableRowLabel("Secret");
                ImGui::Checkbox("##secret", &environment->secret);

                ImGui::TableRowLabel("Wind");
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat3("##wind", &environment->wind.x, 1);

                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    snapshot = true;
                }

                if (ImGui::Button("Set from edge")) {
                    auto face = Face::FromSide(level, Editor::Selection.Tag());
                    environment->wind = face.VectorForEdge(Editor::Selection.Point);
                    snapshot = true;
                }

                ImGui::TableRowLabel("Wind speed");
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##wind-speed", &environment->windSpeed, 1);

                if (ImGui::IsItemDeactivatedAfterEdit())
                    snapshot = true;

                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Fog", &environment->useFog)) {
                    snapshot = true;
                    Editor::Events::LevelChanged();
                }

                ImGui::TableNextColumn();

                {
                    ImGui::BeginDisabled(!environment->useFog);
                    ImGui::ColorEdit3("##fog", &environment->fog.x, ImGuiColorEditFlags_NoInputs);

                    if (ImGui::IsItemDeactivatedAfterEdit())
                        snapshot = true;

                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-1);
                    ImGui::DragFloat("##fogdensity", &environment->fog.w, 0.5f, 1, 100, "%.1f");

                    if (ImGui::IsItemDeactivatedAfterEdit())
                        snapshot = true;

                    ImGui::EndDisabled();
                }

                {
                    DisableControls disable(!environment->useFog);
                    ImGui::TableRowLabel("Additive");
                    snapshot |= ImGui::Checkbox("##additivefog", &environment->additiveFog);
                }

                //{
                //    ImGui::TableRowLabel("Reverb");

                //    auto& reverb = _environment.reverb;

                //    ImGui::SetNextItemWidth(-1);
                //    if (ImGui::BeginCombo("##Reverb", Sound::REVERB_LABELS.at(reverb), ImGuiComboFlags_HeightLarge)) {
                //        for (const auto& item : Sound::REVERB_LABELS | views::keys) {
                //            if (ImGui::Selectable(Sound::REVERB_LABELS.at(item), item == reverb)) {
                //                reverb = item;

                //                //for (auto& marked : GetSelectedSegments())
                //                //    if (auto markedSeg = Game::Level.TryGetSegment(marked))
                //                //        markedSeg->Reverb = (uint8)reverb;

                //                //changed = snapshot = true;
                //            }
                //        }

                //        ImGui::EndCombo();
                //    }
                //}

                ImGui::TableRowLabel("Damage");
                ImGui::SetNextItemWidth(-1);
                ImGui::DragFloat("##damage", &environment->damage, 1, 0, 0, "%.2f");

                ImGui::TableRowLabel("Damage sound");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##damagesound", environment->damageSound.data(), environment->damageSound.capacity());

                ImGui::TableRowLabel("Ambient sound");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##ambientsound", environment->ambientSound.data(), environment->ambientSound.capacity());

                ImGui::EndTable();
            }

            if (snapshot)
                Editor::History.SnapshotLevel("Change environment");
        }

    private:
        static void SetEnvironmentSegments(Level& level, Environment& env, const List<SegID>& segids) {
            env.segments = segids;

            RelinkEnvironments(level);
            Render::UpdateFogFlag = true;
            Editor::Events::LevelChanged();
        }

        static void AddEnvironmentSegments(Level& level, Environment& env, const List<SegID>& segids) {
            Set<SegID> set = { env.segments.begin(), env.segments.end() };
            Seq::insert(set, segids);

            env.segments = Seq::ofSet(set);

            RelinkEnvironments(level);
            Render::UpdateFogFlag = true;
            Editor::Events::LevelChanged();
        }
    };
}
