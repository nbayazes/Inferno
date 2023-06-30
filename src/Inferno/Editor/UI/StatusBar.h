#pragma once

#include "WindowBase.h"

namespace Inferno::Editor {
    class StatusBar : public WindowBase {
    public:
        StatusBar() : WindowBase("Status", nullptr, ToolbarFlags) {
            DefaultWidth = 0;
            DefaultHeight = 0;
        }
        ImVec2 Position;
        float Width = 0;
        float Height = 40 * Shell::DpiScale;

    protected:
        void BeforeUpdate() override {
            float height = ImGui::GetTextLineHeight() + 6 * Shell::DpiScale;
            ImGui::SetNextWindowPos({ Position.x, Position.y });
            ImGui::SetNextWindowSize({ Width, height });
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 0));
        }

        void AfterUpdate() override {
            ImGui::PopStyleVar(3);
        }

        void OnUpdate() override {
            auto& level = Game::Level;

            if (!ImGui::BeginTable("statusbar", 5, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV, {}, Width))
                return;

            ImGui::TableSetupColumn("status", 0, Width - (130 + 150 + 150 + 150) * Shell::DpiScale);
            ImGui::TableSetupColumn("c1", 0, 130 * Shell::DpiScale);
            ImGui::TableSetupColumn("c2", 0, 150 * Shell::DpiScale);
            ImGui::TableSetupColumn("c3", 0, 150 * Shell::DpiScale);
            ImGui::TableSetupColumn("c4", 0, 150 * Shell::DpiScale);
            //ImGui::TableSetupColumn("c5", 0, 200 * Shell::DpiScale);
            
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text(Editor::StatusText.c_str());

            if (level.SegmentExists(Editor::Selection.Segment)) {
                ImGui::TableNextColumn();
                auto [seg, side] = level.GetSegmentAndSide(Selection.Tag());

                switch (Settings::Editor.SelectionMode) {
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

                ImGui::TableNextColumn();
                ImGui::Text("T1: %i T2: %i", side.TMap, side.TMap2);

                ImGui::TableNextColumn();
                auto vertIndex = seg.GetVertexIndex(Selection.Side, Selection.Point);
                //auto& vert = level.Vertices[vertIndex];
                //ImGui::Text("Vert %i: %.1f, %.1f, %.1f", vertIndex, vert.x, vert.y, vert.z);
                ImGui::Text("Pt: %i Vert: %i", Editor::Selection.Point, vertIndex);
            }
            else {
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
            }

            ImGui::TableNextColumn();
            ImGui::Text("Seg: %i:%i",
                        Editor::Selection.Segment,
                        Editor::Selection.Side);

            if (ImGui::IsItemHovered())
                ShowStatsTooltip();


            ImGui::EndTable();

            Height = ImGui::GetWindowHeight();
        }

        static void ShowStatsTooltip() {
            ImGui::BeginTooltip();

            auto& level = Game::Level;

            if (ImGui::BeginTable("count", 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("c1_", 0, 80 * Shell::DpiScale);
                ImGui::TableSetupColumn("c2_", 0, 80 * Shell::DpiScale);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Segments");
                ImGui::TableNextColumn();
                ImGui::Text("%i", level.Segments.size());

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Vertices");
                ImGui::TableNextColumn();
                ImGui::Text("%i", level.Vertices.size());

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Walls");
                ImGui::TableNextColumn();
                ImGui::Text("%i", level.Walls.size());

                ImGui::EndTable();
            }

            ImGui::EndTooltip();
        }
    };
}