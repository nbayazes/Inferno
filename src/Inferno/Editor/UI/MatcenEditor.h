#pragma once
#include "WindowBase.h"
#include "Game.h"

namespace Inferno::Editor {
    class MatcenEditor : public ModalWindowBase {
        uint32 _robots = 0, _robots2 = 0;
        Matcen* _matcen = nullptr;

    public:
        MatcenEditor() : ModalWindowBase("Matcen Editor") {
            Width = 500;
            //Height = 500;
        };

        MatcenID ID = MatcenID::None;

    protected:
        bool OnOpen() override {
            auto matcen = Game::Level.TryGetMatcen(ID);
            if (!matcen) throw Exception("Matcen ID is not valid");

            _robots = matcen->Robots;
            _robots2 = matcen->Robots2;
            return true;
        }

        void OnUpdate() override {
            //ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
            ImGui::Columns(2, "columns", false);

            // each bit corresponds to a robot id
            const uint maxRobots = Game::Level.IsDescent1() ? 24 : 64;
            //ImGui::BeginChild("##matcen", ImVec2(400, 250));

            static int selectedAddRobot = -1, selectedDelRobot = -1;

            auto AddRobot = [&] {
                if (selectedAddRobot == -1) return;
                if (selectedAddRobot < 32)
                    _robots |= (1 << (selectedAddRobot % 32));
                else
                    _robots2 |= (1 << (selectedAddRobot % 32));
            };

            auto RemoveRobot = [&] {
                if (selectedDelRobot == -1) return;
                if (selectedDelRobot < 32)
                    _robots &= ~(1 << (selectedDelRobot % 32));
                else
                    _robots2 &= ~(1 << (selectedDelRobot % 32));
            };

            {
                ImGui::BeginChild("##available", ImVec2(-1, 400), true);

                for (uint i = 0; i < maxRobots; i++) {
                    bool flagged = i < 32
                        ? _robots & (1 << (i % 32))
                        : _robots2 & (1 << (i % 32));

                    if (!flagged) {
                        if (ImGui::Selectable(Resources::GetRobotName(i).c_str(), selectedAddRobot == (int)i,
                                              ImGuiSelectableFlags_AllowDoubleClick)) {
                            selectedAddRobot = i;
                            if (ImGui::IsMouseDoubleClicked(0)) AddRobot();
                        }
                    }
                }

                ImGui::EndChild();
            }

            ImGui::NextColumn();

            //ImGui::SameLine(0, 20);
            {
                ImGui::BeginChild("##active", ImVec2(-1, 400), true);

                for (uint i = 0; i < maxRobots; i++) {
                    bool flagged = i < 32
                        ? _robots & (1 << (i % 32))
                        : _robots2 & (1 << (i % 32));

                    if (flagged) {
                        if (ImGui::Selectable(Resources::GetRobotName(i).c_str(), selectedDelRobot == (int)i,
                                              ImGuiSelectableFlags_AllowDoubleClick)) {
                            selectedDelRobot = i;
                            if (ImGui::IsMouseDoubleClicked(0)) RemoveRobot();
                        }
                    }
                }

                ImGui::EndChild();

            }
            ImGui::NextColumn();

            if (ImGui::Button("Add##addmatcenrbt", { 100, 0 }) && selectedAddRobot != -1)
                AddRobot();

            //ImGui::SameLine(170);
            ImGui::NextColumn();

            if (ImGui::Button("Remove##delmatcenrbt", { 100, 0 }) && selectedDelRobot != -1)
                RemoveRobot();

            //ImGui::EndChild();

            ImGui::Dummy({ 0, 10 * Shell::DpiScale });
            ImGui::Separator();

            ImGui::Columns();

            AcceptButtons();
            //ImGui::PopStyleVar();
        }

        void OnAccept() override {
            if (auto matcen = Game::Level.TryGetMatcen(ID)) {
                matcen->Robots = _robots;
                matcen->Robots2 = _robots2;

                for (auto& marked : GetSelectedSegments()) {
                    if (auto seg = Game::Level.TryGetSegment(marked)) {
                        if (seg->Type != SegmentType::Matcen) continue;

                        if (auto m = Game::Level.TryGetMatcen(seg->Matcen)) {
                            m->Robots = _robots;
                            m->Robots2 = _robots2;
                        }
                    }
                }

                Editor::History.SnapshotLevel("Change matcen robots");
            }
        }
    };
}