#pragma once

#include "WindowBase.h"
#include "../Editor.Object.h"

namespace Inferno::Editor {
    class StatusBar : public WindowBase {
    public:
        StatusBar() : WindowBase("Status", nullptr, ToolbarFlags) {
            DefaultWidth = 0;
            DefaultHeight = 0;
        }
        ImVec2 Position;
        float Width = 0;
        float Height = 40;

    protected:
        void BeforeUpdate() override {
            float height = ImGui::GetTextLineHeight() + 16;
            ImGui::SetNextWindowPos({ Position.x, Position.y });
            ImGui::SetNextWindowSize({ Width, height });
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        }

        void AfterUpdate() override {
            ImGui::PopStyleVar(2);
        }

        void OnUpdate() override {
            constexpr int ItemWidth = 150;
            constexpr int RightStatusWidth = int(ItemWidth * 3.5f) + 350;

            {
                ImGui::BeginChild("StatusText", { Width - RightStatusWidth, 0 }, false, ImGuiWindowFlags_NoInputs);
                ImGui::Text(Editor::StatusText.c_str());

                //ImGui::Text("wants capture: %i", ImGui::GetIO().WantCaptureMouse);
                //ImGui::SameLine(0, 20);
                //ImGui::Text("hovered: %i", ImGui::GetCurrentContext()->HoveredWindow == nullptr);

                ImGui::EndChild();
            }

            auto& level = Game::Level;

            if (!level.SegmentExists(Editor::Selection.Segment)) return;
            auto [seg, side] = level.GetSegmentAndSide(Selection.Tag());

            ImGui::SameLine(Width - RightStatusWidth - ItemWidth, 5);
            {
                ImGui::BeginChild("selection", { 135, 0 });
                //ImGui::PushStyleColor(ImGuiCol_Text, { 0.9f, 0.9f, 1.0f, 1.0f });
                switch (Settings::SelectionMode) {
                    case SelectionMode::Point:
                    case SelectionMode::Edge:
                        ImGui::Text("Marked: %i", Editor::Marked.Points.size());
                        break;

                    default:
                    case SelectionMode::Face:
                        ImGui::Text("Marked: %i", Editor::Marked.Faces.size());
                        break;

                    case SelectionMode::Segment:
                        ImGui::Text("Marked: %i", Editor::Marked.Segments.size());
                        break;

                    case SelectionMode::Object:
                        ImGui::Text("Marked: %i", Editor::Marked.Objects.size());
                        break;
                }
                //ImGui::PopStyleColor();
                ImGui::EndChild();
                ImGui::SeparatorVertical();
                ImGui::SameLine(0, 10);
            }


            ImGui::SameLine(Width - RightStatusWidth, 5);
            {
                ImGui::BeginChild("segchild", { ItemWidth, 0 });
                ImGui::Text("Seg: %i:%i:%i",
                            Editor::Selection.Segment,
                            Editor::Selection.Side,
                            Editor::Selection.Point);
                //ImGui::SameLine(0, 10);
                //ImGui::Text("%i", side.Type);
                ImGui::EndChild();
            }


            ImGui::SeparatorVertical();
            ImGui::SameLine(0, 10);
            //ImGui::SameLine(Width - RightStatusWidth + 150, 0);
            //ImGui::Text("Marked S: %i F: %i P: %i O: %i", 
            //            Editor::Marked.Segments.size(),
            //            Editor::Marked.Faces.size(),
            //            Editor::Marked.Points.size(),
            //            Editor::Marked.Objects.size());

            {
                ImGui::BeginChild("segcount", { ItemWidth * 1.5, 0 });
                ImGui::Text("Segs: %i Verts: %i", level.Segments.size(), level.Vertices.size());
                ImGui::EndChild();

                if (ImGui::IsItemHovered()) {
                    ImGui::SetNextWindowSize({ 350, 0 });
                    ImGui::BeginTooltip();
                    ImGui::Columns(3, nullptr, false);
                    ImGui::SetColumnWidth(1, 80);
                    ImGui::SetColumnWidth(2, 150);

                    ImGui::NextColumn();

                    ImGui::TextDisabled("Count");
                    ImGui::NextColumn();

                    ImGui::TextDisabled("Limit");
                    ImGui::NextColumn();

                    ImGui::ColumnLabel("Segments");
                    ImGui::ColumnLabel("%i", level.Segments.size());
                    ImGui::ColumnLabel("%i (9000)", level.Limits.Segments);

                    ImGui::ColumnLabel("Vertices");
                    ImGui::ColumnLabel("%i", level.Vertices.size());
                    ImGui::ColumnLabel("%i (36000)", level.Limits.Vertices);

                    ImGui::ColumnLabel("Walls");
                    ImGui::ColumnLabel("%i", level.Walls.size());
                    ImGui::ColumnLabel("%i (254)", level.Limits.Walls);

                    ImGui::ColumnLabel("Triggers");
                    ImGui::ColumnLabel("%i", level.Triggers.size());
                    ImGui::ColumnLabel("%i", level.Limits.Triggers);

                    ImGui::ColumnLabel("Matcens");
                    ImGui::ColumnLabel("%i", level.Matcens.size());
                    ImGui::ColumnLabel("%i", level.Limits.Matcens);

                    ImGui::ColumnLabel("F. lights");
                    ImGui::ColumnLabel("%i", level.FlickeringLights.size());
                    ImGui::ColumnLabel("%i", level.Limits.FlickeringLights);

                    ImGui::ColumnLabel("Players");
                    ImGui::ColumnLabel("%i", GetObjectCount(level, ObjectType::Player));
                    ImGui::ColumnLabel("%i", level.Limits.Players);

                    ImGui::ColumnLabel("Co-op");
                    ImGui::ColumnLabel("%i", GetObjectCount(level, ObjectType::Coop));
                    ImGui::ColumnLabel("%i", level.Limits.Coop);
                    
                    ImGui::Columns(1);
                    ImGui::EndTooltip();

                    //ImGui::OpenPopup("limits");
                    //if (ImGui::BeginPopup("limits")) {
                    //    ImGui::EndPopup();
                    //}
                }

            }

            ImGui::SeparatorVertical();
            ImGui::SameLine(0, 10);

            {
                ImGui::BeginChild("tmap", { ItemWidth, 0 });
                ImGui::Text("T1: %i T2: %i", side.TMap, side.TMap2);
                ImGui::EndChild();
            }

            ImGui::SeparatorVertical();
            ImGui::SameLine(0, 10);

            {
                ImGui::BeginChild("pointinfo"/*, { ItemWidth * 2.5f, 0 }*/);
                auto vertIndex = seg.GetVertexIndex(Selection.Side, Selection.Point);
                auto& vert = level.Vertices[vertIndex];
                ImGui::Text("Point %i: %.1f, %.1f, %.1f", vertIndex, vert.x, vert.y, vert.z);
                ImGui::EndChild();
            }

            Height = ImGui::GetWindowHeight();
        }
    };
}