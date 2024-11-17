#pragma once

#include "WindowBase.h"
#include "Editor/Editor.Diagnostics.h"
#include "Camera.h"

namespace Inferno::Editor {
    class DiagnosticWindow final : public WindowBase {
        List<SegmentDiagnostic> _segments;
        List<SegmentDiagnostic> _objects;
        int _selection{};
        bool _showWarnings = false, _markErrors = false, _fixErrors = true, _checkDegeneracy = false;
        bool _checked = false; // user has checked the level once already
        bool _showStats = true;
        bool _countedObjects = false;

        uint _powerups = 0;
        uint _robots = 0;

    public:
        DiagnosticWindow() : WindowBase("Diagnostics", &Settings::Editor.Windows.Diagnostics) {
            auto onLevelChanged = [this] {
                if (IsOpen()) {
                    if (_checked) CheckLevel(_fixErrors);
                    _countedObjects = false;
                }
            };

            Events::SegmentsChanged += onLevelChanged;
            Events::ObjectsChanged += onLevelChanged;
            Events::SnapshotChanged += [this] {
                if (IsOpen() && _checked) CheckLevel(false);
            };

            Events::LevelLoaded += [this] {
                _checked = false;
                _segments.clear();
                _objects.clear();
            };
        }

    protected:
        void CountObjects() {
            _powerups = _robots = 0;

            for (auto& obj : Game::Level.Objects) {
                if (obj.Type == ObjectType::Powerup) _powerups++;
                if (obj.Type == ObjectType::Robot) _robots++;
            }
        }

        void CheckLevel(bool fixErrors) {
            _checked = true;
            _segments = CheckSegments(Game::Level, fixErrors, _checkDegeneracy);
            _objects = CheckObjects(Game::Level);

            if (_markErrors) {
                Editor::Marked.Segments.clear();
                for (auto& s : _segments)
                    Editor::Marked.Segments.insert(s.Tag.Segment);

                /*Editor::Marked.Objects.clear();
                for (auto& s : _objects)
                    Editor::Marked.Objects.insert(s.Tag.Segment);*/
            }
        }

        void OnUpdate() override {
            if (!_countedObjects) {
                CountObjects();
                _countedObjects = true;
            }

            if (ImGui::Button("Check level"))
                CheckLevel(_fixErrors);

            //ImGui::SameLine();
            //ImGui::Checkbox("Warnings", &_showWarnings);
            //if (ImGui::BeginCombo("##csys-dropdown", "Settings", ImGuiComboFlags_HeightLarge)) {
            //    ImGui::Text("Coordinate system");

            //}

            ImGui::SameLine();
            constexpr auto popupKey = "options-popup";
            if (ImGui::Button("Options..."))
                ImGui::OpenPopup(popupKey);

            if (ImGui::BeginPopup(popupKey)) {
                ImGui::MenuItem("Fix errors", "", &_fixErrors);
                ImGui::MenuItem("Mark errors", "", &_markErrors);
                ImGui::MenuItem("Show degenerate", "", &_checkDegeneracy);
                ImGui::EndPopup();
            }

            const char* toggleLabel = _showStats ? "Hide stats" : " Show stats";
            const auto toggleButtonWidth = 140 * Shell::DpiScale;
            ImGui::SameLine(ImGui::GetWindowWidth() - toggleButtonWidth);
            if (ImGui::Button(toggleLabel, { toggleButtonWidth, 0 }))
                _showStats = !_showStats;

            const auto statsWidth = _showStats ? 280 * Shell::DpiScale : 0;
            ImGui::BeginChild("diag_list", { ImGui::GetWindowWidth() - statsWidth, 0 });

            constexpr auto flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
            if (ImGui::BeginTable("seg_table", 2, flags)) {
                ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible
                ImGui::TableSetupColumn("Seg", ImGuiTableColumnFlags_WidthFixed/*, ImGuiTableColumnFlags_DefaultSort, 0.0f*//*, MyItemColumnID_ID*/);
                ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthFixed/*, MyItemColumnID_Name*/);
                ImGui::TableHeadersRow();

                constexpr auto selectable_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap;

                for (int i = 0; i < _segments.size(); i++) {
                    auto& item = _segments[i];
                    bool selected = i == _selection;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    auto segLabel = item.Tag.Side == SideID::None ? fmt::format("{}", (int)item.Tag.Segment) : fmt::format("{}:{}", (int)item.Tag.Segment, (int)item.Tag.Side);

                    ImGui::Text(segLabel.c_str());

                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    if (ImGui::Selectable(item.Message.c_str(), selected, selectable_flags, ImVec2(0, 0))) {
                        _selection = i;
                        Editor::Selection.SetSelection(item.Tag);
                        auto& seg = Game::Level.GetSegment(item.Tag);
                        Render::Camera.LerpTo(seg.Center, 0.25f);
                    }
                    ImGui::PopID();
                }

                for (int i = 0; i < _objects.size(); i++) {
                    auto& item = _objects[i];
                    bool selected = i + (int)_segments.size() == _selection;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    if (ImGui::Selectable(item.Message.c_str(), selected, selectable_flags, ImVec2(0, 0))) {
                        _selection = i + (int)_segments.size();
                        //Editor::Selection.SetSelection(item.Tag);
                        //auto& seg = Game::Level.GetSegment(item.Tag);
                        //Render::Camera.LerpTo(seg.Center, 0.25f);
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            ImGui::EndChild();

            if (_showStats) {
                ImGui::SameLine();
                ImGui::BeginChild("stats", { statsWidth, 0 });

                if (ImGui::BeginTable("diag_table", 3, flags)) {
                    auto& level = Game::Level;

                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed/*, ImGuiTableColumnFlags_DefaultSort, 0.0f*//*, MyItemColumnID_ID*/);
                    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed/*, ImGuiTableColumnFlags_PreferSortDescending, 0.0f*//*, MyItemColumnID_Quantity*/);
                    ImGui::TableSetupColumn("Limit", ImGuiTableColumnFlags_WidthFixed/*, ImGuiTableColumnFlags_PreferSortDescending, 0.0f*//*, MyItemColumnID_Quantity*/);
                    ImGui::TableHeadersRow();

                    ImGui::TableRowLabel("Segments");
                    ImGui::Text("%i", level.Segments.size());
                    ImGui::TableNextColumn();
                    ImGui::Text("%i (9000)", level.Limits.Segments);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Most source ports have a maximum of 9000 segments");

                    ImGui::TableRowLabel("Vertices");
                    ImGui::Text("%i", level.Vertices.size());
                    ImGui::TableNextColumn();
                    ImGui::Text("%i (36000)", level.Limits.Vertices);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Most source ports have a maximum of 36000 vertices");

                    ImGui::TableRowLabel("Objects");
                    ImGui::Text("%i", level.Objects.size());
                    ImGui::TableNextColumn();
                    ImGui::Text("%i", level.Limits.Objects);

                    ImGui::TableRowLabel("Powerups");
                    ImGui::Text("%i", _powerups);

                    ImGui::TableRowLabel("Robots");
                    ImGui::Text("%i", _robots);

                    ImGui::TableRowLabel("Walls");
                    ImGui::Text("%i (%i)", level.Walls.Size(), level.Walls.ShrinkableSize());
                    ImGui::TableNextColumn();
                    ImGui::Text("%i", level.Limits.Walls);

                    ImGui::TableRowLabel("Triggers");
                    ImGui::Text("%i", level.Triggers.size());
                    ImGui::TableNextColumn();
                    ImGui::Text("%i", level.Limits.Triggers);

                    ImGui::TableRowLabel("Matcens");
                    ImGui::Text("%i", level.Matcens.size());
                    ImGui::TableNextColumn();
                    ImGui::Text("%i", level.Limits.Matcens);

                    ImGui::TableRowLabel("F. lights");
                    ImGui::Text("%i", level.FlickeringLights.size());
                    ImGui::TableNextColumn();
                    ImGui::Text("%i", level.Limits.FlickeringLights);

                    ImGui::TableRowLabel("Players");
                    ImGui::Text("%i", GetObjectCount(level, ObjectType::Player));
                    ImGui::TableNextColumn();
                    ImGui::Text("%i", level.Limits.Players);

                    ImGui::TableRowLabel("Co-op");
                    ImGui::Text("%i", GetObjectCount(level, ObjectType::Coop));
                    ImGui::TableNextColumn();
                    ImGui::Text("%i", level.Limits.Coop);

                    ImGui::EndTable();
                }

                ImGui::EndChild();
            }
        }
    };
}
