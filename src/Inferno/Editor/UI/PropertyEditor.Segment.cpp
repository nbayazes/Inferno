#include "pch.h"

#include "Game.Segment.h"
#include "PropertyEditor.h"
#include "../Editor.h"
#include "Editor/Editor.Segment.h"
#include "Editor/Editor.Wall.h"
#include "Graphics/Render.h"
#include "Editor/Editor.Lighting.h"

namespace Inferno::Editor {
    // Sets snapshot to true when the previous item finishes editing
    void CheckForSnapshot(bool& snapshot) {
        if (ImGui::IsItemDeactivatedAfterEdit()) snapshot = true;
    }

    bool TriggerTypesDropdown(int& value) {
        static const char* triggerTypeLabels[] = {
            "None", "Open Door", "Close Door", "Matcen", "Exit", "Secret Exit", "Illusion Off",
            "Illusion On", "Unlock Door", "Lock Door", "Open Wall", "Close Wall", "Illusory Wall",
            "Light Off", "Light On"
        };

        bool changed = false;

        if (ImGui::BeginCombo("##triggertype", triggerTypeLabels[value], ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < std::size(triggerTypeLabels); i++) {
                const bool isSelected = i == value;
                if (ImGui::Selectable(triggerTypeLabels[i], isSelected)) {
                    value = i;
                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    bool TriggerTargetsPicker(Level& level, Trigger& trigger, TriggerID tid) {
        bool changed = false;
        ImGui::TableRowLabel("Targets");
        ImGui::BeginChild("trigger-targets", { -1, 130 * Shell::DpiScale }, true);

        static int selectedIndex = 0;

        for (int i = 0; i < trigger.Targets.Count(); i++) {
            auto& target = trigger.Targets[i];
            string targetLabel = fmt::format("{}:{}", (int)target.Segment, (int)target.Side);
            if (ImGui::Selectable(targetLabel.c_str(), selectedIndex == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                selectedIndex = i;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    Editor::Selection.SetSelection(target);
                }
            }
        }

        ImGui::EndChild();

        ImVec2 btnSize = { 100 * Shell::DpiScale, 0 };

        if (ImGui::Button("Add##TriggerTarget", btnSize)) {
            if (Editor::Marked.Faces.empty())
                ShowWarningMessage(L"Please mark faces to add as targets.");

            for (auto& mark : Editor::Marked.Faces) {
                AddTriggerTarget(level, tid, mark);
                changed = true;
            }
        }

        float contentWidth = ImGui::GetWindowContentRegionMax().x;

        if (ImGui::GetCursorPosX() + btnSize.x * 2 + 5 < contentWidth)
            ImGui::SameLine();

        if (ImGui::Button("Remove##TriggerTarget", btnSize)) {
            RemoveTriggerTarget(level, tid, selectedIndex);
            if (selectedIndex > trigger.Targets.Count()) selectedIndex--;
            changed = true;
        }
        return changed;
    }

    bool TriggerPropertiesD1(Level& level, WallID wid) {
        bool snapshot = false;
        auto wall = level.TryGetWall(wid);
        DisableControls disable(!wall);

        auto trigger = wall ? level.TryGetTrigger(wall->Trigger) : nullptr;

        bool open = ImGui::TableBeginTreeNode("Trigger");

        if (!trigger) {
            if (ImGui::Button("Add", { 100 * Shell::DpiScale, 0 }) && wall)
                wall->Trigger = AddTrigger(level, wid, TriggerType::OpenDoor);
        }
        else {
            if (ImGui::Button("Remove", { 100 * Shell::DpiScale, 0 }))
                RemoveTrigger(level, wall->Trigger);
        }

        if (open) {
            if (trigger && wall) {
                ImGui::TableRowLabel("ID");
                ImGui::Text("%i", wall->Trigger);

                snapshot |= TriggerTargetsPicker(level, *trigger, wall->Trigger);

                ImGui::TableRowLabel("Open door");
                snapshot |= FlagCheckbox("##Open door", TriggerFlagD1::OpenDoor, trigger->FlagsD1);

                ImGui::TableRowLabel("Exit");
                snapshot |= FlagCheckbox("##Exit", TriggerFlagD1::Exit, trigger->FlagsD1);

                ImGui::TableRowLabel("Secret exit");
                snapshot |= FlagCheckbox("##Secret exit", TriggerFlagD1::SecretExit, trigger->FlagsD1);

                ImGui::TableRowLabel("Matcen");
                snapshot |= FlagCheckbox("##Matcen", TriggerFlagD1::Matcen, trigger->FlagsD1);

                ImGui::TableRowLabel("Illusion off");
                snapshot |= FlagCheckbox("##IllusionOff", TriggerFlagD1::IllusionOff, trigger->FlagsD1);

                ImGui::TableRowLabel("Illusion on");
                snapshot |= FlagCheckbox("##IllusionOn", TriggerFlagD1::IllusionOn, trigger->FlagsD1);
            }
            else {
                ImGui::TextDisabled("No trigger");
            }

            ImGui::TreePop();
        }

        return snapshot;
    }

    bool TriggerPropertiesD2(Level& level, WallID wallId) {
        bool snapshot = false;
        auto wall = level.TryGetWall(wallId);
        auto tid = level.GetTriggerID(wallId);
        auto trigger = level.TryGetTrigger(wallId);
        DisableControls disable(!wall);
        bool open = ImGui::TableBeginTreeNode("Trigger");

        {
            // Shift values by 1 to use 0 as "None"
            auto type = trigger ? (int)trigger->Type + 1 : 0;

            ImGui::SetNextItemWidth(-1);
            if (TriggerTypesDropdown(type)) {
                if (type == 0) {
                    RemoveTrigger(level, tid);
                }
                else {
                    auto tt = TriggerType(type - 1);
                    if (trigger) {
                        trigger->Type = tt;
                    }
                    else {
                        tid = AddTrigger(level, wallId, tt);
                    }
                }
                snapshot = true;
            }

            trigger = level.TryGetTrigger(wallId);
        }

        if (open) {
            if (trigger) {
                ImGui::TableRowLabel("ID");
                ImGui::Text("%i", tid);

                snapshot |= TriggerTargetsPicker(level, *trigger, tid);

                ImGui::TableRowLabel("No message");
                snapshot |= FlagCheckbox("##No Message", TriggerFlag::NoMessage, trigger->Flags);

                ImGui::TableRowLabel("One shot");
                snapshot |= FlagCheckbox("##One shot", TriggerFlag::OneShot, trigger->Flags);
            }
            else {
                ImGui::TextDisabled("No trigger");
            }

            ImGui::TreePop();
        }

        return snapshot;
    }

    bool FlickeringProperties(Level& level, Tag tag) {
        auto light = level.GetFlickeringLight(tag);
        bool open = ImGui::TableBeginTreeNode("Flickering light");
        bool snapshot = false;

        if (!light) {
            DisableControls disable(!CanAddFlickeringLight(level, tag));
            if (ImGui::Button("Add", { 100 * Shell::DpiScale, 0 }))
                Commands::AddFlickeringLight();
        }
        else {
            if (ImGui::Button("Remove", { 100 * Shell::DpiScale, 0 }))
                Commands::RemoveFlickeringLight();
        }

        light = level.GetFlickeringLight(tag);

        if (open) {
            if (light) {
                auto delay = light->Delay * 1000;
                auto orig = *light;
                ImGui::TableRowLabel("Delay");

                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat("##Delay", &delay, 10.0f, 10, 1000, "%.0f ms"))
                    light->Delay = delay / 1000;

                CheckForSnapshot(snapshot);

                char mask[33]{};
                for (int i = 0; i < 32; i++)
                    mask[31 - i] = (light->Mask >> i) & 0x1 ? '1' : '0';

                ImGui::TableRowLabel("Mask");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputTextEx("##Mask", nullptr, mask, 33, { -1, 0 }, 0)) {
                    for (int i = 0; i < 32; i++) {
                        if (mask[31 - i] == '1')
                            light->Mask |= 1 << i;
                        else
                            light->Mask &= ~(1 << i);
                    }
                }

                CheckForSnapshot(snapshot);

                if (ImGui::Button("Shift Left", { 100 * Shell::DpiScale, 0 })) {
                    light->ShiftLeft();
                    snapshot = true;
                }

                ImGui::SameLine(0, 5);
                if (ImGui::Button("Shift Right", { 100 * Shell::DpiScale, 0 })) {
                    light->ShiftRight();
                    snapshot = true;
                }

                if (ImGui::Button("Defaults..."))
                    ImGui::OpenPopup("FlickerDefaults");

                ImGui::SetNextWindowSize({ 100 * Shell::DpiScale, -1 });
                if (ImGui::BeginPopup("FlickerDefaults")) {
                    auto flickerDefault = [&light, &snapshot](const char* name, uint32 mask) {
                        if (ImGui::Selectable(name)) {
                            light->Mask = mask;
                            snapshot = true;
                        }
                    };

                    flickerDefault("On", FlickeringLight::Defaults::On);
                    flickerDefault("Off", 0);
                    flickerDefault("Strobe / 4", FlickeringLight::Defaults::Strobe4);
                    flickerDefault("Strobe / 8", FlickeringLight::Defaults::Strobe8);
                    flickerDefault("Flicker", FlickeringLight::Defaults::Flicker);
                    ImGui::EndPopup();
                }

                // Update selected faces
                if (orig.Delay != light->Delay || orig.Mask != light->Mask) {
                    for (auto& face : GetSelectedFaces()) {
                        if (auto l = level.GetFlickeringLight(face)) {
                            if (orig.Delay != light->Delay) l->Delay = light->Delay;
                            if (orig.Mask != light->Mask) l->Mask = light->Mask;
                        }
                    }
                }
            }
            else {
                ImGui::TextDisabled("No light");
            }
            ImGui::TreePop();
        }

        return snapshot;
    }

    bool SegmentTypeDropdown(SegmentType& type) {
        static constexpr const char* SegmentTypeLabels[] = {
            "None", "Energy", "Repair", "Reactor", "Matcen", "Blue Goal", "Red Goal"
        };

        bool snapshot = false;

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##segtype", SegmentTypeLabels[(int)type])) {
            for (int i = 0; i < std::size(SegmentTypeLabels); i++) {
                if (i == 2) continue;

                const bool isSelected = (int)type == i;
                if (ImGui::Selectable(SegmentTypeLabels[i], isSelected)) {
                    snapshot = true;
                    type = (SegmentType)i;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return snapshot;
    }

    string GetMatcenRobotLabel(const Level& level, const Matcen& matcen) {
        string label;

        const uint maxRobots = level.IsDescent1() ? 25 : 64;
        for (uint i = 0; i < maxRobots; i++) {
            bool flagged = i < 32
                ? matcen.Robots & (1 << (i % 32))
                : matcen.Robots2 & (1 << (i % 32));

            if (flagged)
                label += (label.empty() ? "" : ", ") + Resources::GetRobotName(i);
        }

        return label;
    }

    void MatcenProperties(Level& level, MatcenID id, MatcenEditor& editor) {
        auto matcen = level.TryGetMatcen(id);
        if (!matcen) {
            ImGui::Text("Matcen data is missing!");
            return;
        }

        ImGui::TableRowLabel("Robots");
        auto robotLabel = GetMatcenRobotLabel(level, *matcen);
        if (!robotLabel.empty())
            ImGui::TextWrapped(robotLabel.c_str());

        if (ImGui::Button("Edit", { 100 * Shell::DpiScale, 0 })) {
            editor.ID = id;
            editor.Show();
        }
    }

    Option<Color> SideLightBuffer;

    ImVec4 GetPreviewColor(Color color) {
        auto max = std::max({ color.x, color.y, color.z });
        if (max > 0) color.w = 1 / max;
        color.Premultiply();
        return { color.x, color.y, color.z, color.w };
    }

    bool LightPicker(Color& color, bool& snapshot, bool& relightLevel) {
        auto maybeRelightLevel = [&relightLevel] {
            relightLevel = ImGui::IsKeyDown(ImGuiKey::ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey::ImGuiKey_RightCtrl);
        };

        static Color previous;
        Color entryColor = color;

        auto updateMarkedColor = [&color, &entryColor] {
            bool colorChanged = color.x != entryColor.x || color.y != entryColor.y || color.z != entryColor.z;
            bool intensityChanged = color.w != entryColor.w;
            if (!colorChanged && !intensityChanged) return false;

            for (auto& tag : GetSelectedFaces()) {
                if (auto marked = Game::Level.TryGetSide(tag)) {
                    // Only update the corresponding components for each side
                    if (colorChanged) {
                        if (!marked->LightOverride) {
                            marked->LightOverride = color;
                        }
                        else {
                            marked->LightOverride->x = color.x;
                            marked->LightOverride->y = color.y;
                            marked->LightOverride->z = color.z;
                        }
                    }

                    if (intensityChanged) {
                        if (!marked->LightOverride)
                            marked->LightOverride = color;
                        else
                            marked->LightOverride->w = color.w;
                    }
                }
            }

            return true;
        };

        if (ImGui::ColorButton("##ColorPickerButton", GetPreviewColor(color))) {
            ImGui::OpenPopup("ColorPicker");
            previous = color;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::DragFloat("##value", &color.w, 0.01f, 0, 10, "%.2f")) {
            if (color.w < 0) color.w = 0;
        }

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            snapshot = true; // Snapshot after the user releases the mouse button
            maybeRelightLevel();
        }

        if (!ImGui::BeginPopup("ColorPicker"))
            return updateMarkedColor();

        ImGui::ColorPicker3("##picker", &color.x, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);

        if (ImGui::IsItemDeactivatedAfterEdit())
            snapshot = true; // Snapshot after the user releases the mouse button

        ImGui::SameLine();
        {
            ImGui::BeginGroup();

            {
                ImGui::BeginGroup();
                ImGui::Text("Current");
                auto previewColor = GetPreviewColor(color);
                if (ImGui::ColorButton("##current", previewColor, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoDragDrop, ImVec2(60, 40)))
                    maybeRelightLevel();

                // Override ColorButton drag and drop because we want the real color - not the preview color
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F, &color, sizeof(float) * 4, ImGuiCond_Once);
                    ImGui::EndDragDropSource();
                }

                ImGui::EndGroup();
            }

            ImGui::SameLine(0, 20);
            {
                ImGui::BeginGroup();
                ImGui::Text("Previous");
                auto previewColor = GetPreviewColor(previous);
                if (ImGui::ColorButton("##previous", previewColor, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoDragDrop, ImVec2(60, 40))) {
                    color = previous;
                    snapshot = true;
                    maybeRelightLevel();
                }

                // Override ColorButton drag and drop because we want the real color - not the preview color
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F, &color, sizeof(float) * 4, ImGuiCond_Once);
                    ImGui::EndDragDropSource();
                }

                ImGui::EndGroup();
            }

            ImGui::Dummy({ 0, 20 });

            auto& palette = Settings::Editor.Palette;
            static int dragSource = -1;

            for (int n = 0; n < std::size(palette); n++) {
                ImGui::PushID(n);
                if (n % 6 != 0)
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.y);

                ImGuiColorEditFlags paletteButtonFlags = ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop;
                auto previewColor = GetPreviewColor(palette[n]);

                if (ImGui::ColorButton("##palette", previewColor, paletteButtonFlags, ImVec2(32, 32))) {
                    color.x = palette[n].x;
                    color.y = palette[n].y;
                    color.z = palette[n].z;
                    snapshot = true;
                    maybeRelightLevel();
                }

                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F, &color, sizeof(float) * 4, ImGuiCond_Once);
                    ImGui::EndDragDropSource();
                    dragSource = n;
                }

                //if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                //    auto& pcolor = palette[n];
                //    ImGui::ColorConvertRGBtoHSV(pcolor.x, pcolor.y, pcolor.z, h, s, v);
                //    ImGui::SetTooltip("%.2f, %.2f, %.2f v: %.2f", pcolor.x, pcolor.y, pcolor.z, v);
                //}

                // Allow user to drop colors into each palette entry. Note that ColorButton() is already a
                // drag source by default, unless specifying the ImGuiColorEditFlags_NoDragDrop flag.
                if (ImGui::BeginDragDropTarget()) {
                    /*if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F))
                        memcpy(&palette[n], payload->Data, sizeof(float) * 3);*/

                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F)) {
                        if (dragSource != -1) {
                            // Dragged from another palette entry, swap them
                            std::swap(palette[n], palette[dragSource]);
                            dragSource = -1;
                        }
                        else {
                            // Dragged from outside palette
                            memcpy(&palette[n], payload->Data, sizeof(float) * 4);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::PopID();
            }

            ImGui::Text("Intensity");
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##intensity", &color.w, 0.01f, 0, 10, "%.2f");

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                snapshot = true; // Snapshot after the user releases the mouse button
                maybeRelightLevel();
            }

            static float valueIncrement = 0.25f;
            if (ImGui::Button("-.25")) {
                color.w -= valueIncrement;
                if (color.w < 0.0f) color.w = 0.0f;
                maybeRelightLevel();
            }

            ImGui::SameLine();
            if (ImGui::Button("+.25")) {
                color.w += valueIncrement;
                maybeRelightLevel();
            }

            ImGui::Text("Hold ctrl when picking a\ncolor to relight level");
            ImGui::EndGroup();
        }

        ImGui::EndPopup();
        return updateMarkedColor();
    }

    bool SideLighting(Level& level, Segment& seg, SegmentSide& side) {
        bool open = ImGui::TableBeginTreeNode("Light override");
        bool levelChanged = false;
        bool snapshot = false;

        if (open) {
            auto applyToMarkedFaces = [&level](const std::function<void(SegmentSide&)>& action) {
                for (auto& tag : GetSelectedFaces()) {
                    if (auto marked = level.TryGetSide(tag))
                        action(*marked);
                }
            };

            {
                // Light color override
                bool hasOverride = side.LightOverride.has_value();
                auto light = side.LightOverride.value_or(GetLightColor(side, Settings::Editor.Lighting.EnableColor));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Light Color");

                ImGui::TableNextColumn();
                if (ImGui::Button("Copy")) {
                    SideLightBuffer = light;
                }

                ImGui::SameLine();
                if (ImGui::Button("Paste")) {
                    applyToMarkedFaces([](SegmentSide& dest) { dest.LightOverride = SideLightBuffer; });
                    snapshot = true;
                }

                ImGui::SameLine();
                if (ImGui::Button("Select"))
                    Commands::MarkLightColor();

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Color", &hasOverride)) {
                    side.LightOverride = hasOverride ? Option(light) : std::nullopt;
                    applyToMarkedFaces([&side](SegmentSide& dest) { dest.LightOverride = side.LightOverride; });
                    snapshot = true;
                }

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);

                bool relightLevel = false;

                if (LightPicker(light, snapshot, relightLevel)) {
                    side.LightOverride = light;
                    levelChanged = true;
                }

                if (relightLevel)
                    Commands::LightLevel(Game::Level, Settings::Editor.Lighting);
            }

            {
                // Radius override
                bool overrideChanged = false;
                bool hasOverride = side.LightRadiusOverride.has_value();
                auto radius = side.LightRadiusOverride.value_or(Settings::Editor.Lighting.Radius);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Radius", &hasOverride)) {
                    side.LightRadiusOverride = hasOverride ? Option(radius) : std::nullopt;
                    overrideChanged = true;
                    snapshot = true;
                }

                ImGui::TableNextColumn();
                //DisableControls disable(!hasOverride);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##radius", &radius, 10, 50, "%.1f")) {
                    side.LightRadiusOverride = radius;
                    overrideChanged = true;
                }
                CheckForSnapshot(snapshot);

                if (overrideChanged) {
                    levelChanged = true;
                    applyToMarkedFaces([&side](SegmentSide& dest) { dest.LightRadiusOverride = side.LightRadiusOverride; });
                }
            }

            {
                // Light plane override
                bool overrideChanged = false;
                bool hasOverride = side.LightPlaneOverride.has_value();
                auto plane = side.LightPlaneOverride.value_or(Settings::Editor.Lighting.LightPlaneTolerance);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Mode");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);

                // Adjust the 'off' entry so it works in the UI nicely
                auto lightMode = side.LightMode == DynamicLightMode::Off ? DynamicLightMode::Count : side.LightMode;

                if (ImGui::Combo("##mode", (int*)&lightMode, "Steady\0Weak flicker\0Flicker\0Strong flicker\0Pulse\0Big pulse\0Off")) {
                    side.LightMode = lightMode == DynamicLightMode::Count ? DynamicLightMode::Off : lightMode;
                    applyToMarkedFaces([&side](SegmentSide& dest) { dest.LightMode = side.LightMode; });
                    snapshot = true;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Light plane", &hasOverride)) {
                    side.LightPlaneOverride = hasOverride ? Option(plane) : std::nullopt;
                    snapshot = overrideChanged = true;
                }

                ImGui::TableNextColumn();
                DisableControls disable(!hasOverride);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##lightplane", &plane, -0.01f, -1)) {
                    side.LightPlaneOverride = plane;
                    overrideChanged = true;
                }
                CheckForSnapshot(snapshot);

                if (overrideChanged) {
                    levelChanged = true;
                    applyToMarkedFaces([&side](SegmentSide& dest) { dest.LightPlaneOverride = side.LightPlaneOverride; });
                }
            }

            {
                // Occlusion
                ImGui::TableRowLabel("Occlusion");
                if (ImGui::Checkbox("##Occlusion", &side.EnableOcclusion)) {
                    levelChanged = snapshot = true;
                    applyToMarkedFaces([&side](SegmentSide& dest) { dest.EnableOcclusion = side.EnableOcclusion; });
                }
            }

            auto vertexLightSlider = [&levelChanged, &snapshot, &side](const char* label, int point) {
                if (point == (int)Editor::Selection.Point)
                    ImGui::PushStyleColor(ImGuiCol_Text, { 0, 1, 0, 1 });

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox(label, &side.LockLight[point]))
                    snapshot = true;

                if (point == (int)Editor::Selection.Point)
                    ImGui::PopStyleColor();

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                DisableControls disable(!side.LockLight[point]);
                levelChanged |= ImGui::ColorEdit3(fmt::format("##{}", label).c_str(), &side.Light[point].x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                CheckForSnapshot(snapshot);
            };

            vertexLightSlider("Point 0", 0);
            vertexLightSlider("Point 1", 1);
            vertexLightSlider("Point 2", 2);
            vertexLightSlider("Point 3", 3);

            {
                // Volume light
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Volume", &seg.LockVolumeLight))
                    snapshot = true;

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                DisableControls disable(!seg.LockVolumeLight);
                levelChanged |= ImGui::ColorEdit3("##volume", &seg.VolumeLight.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
                CheckForSnapshot(snapshot);
            }

            {
                // Dynamic multiplier
                bool overrideChanged = false;
                bool hasOverride = side.DynamicMultiplierOverride.has_value();
                auto mult = side.DynamicMultiplierOverride.value_or(1);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Dynamic multiplier", &hasOverride)) {
                    side.DynamicMultiplierOverride = hasOverride ? Option(mult) : std::nullopt;
                    overrideChanged = true;
                    snapshot = true;
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
                    ImGui::TextUnformatted("Adjusts the light subtracted by breakable or flickering lights.\nA value of 0.5 would halve the subtracted light.\n\nIntended to make flickering lights less intense.");
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }

                ImGui::TableNextColumn();
                DisableControls disable(!hasOverride);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##dynmult", &mult, 0, 1, "%.2f")) {
                    side.DynamicMultiplierOverride = mult;
                    overrideChanged = true;
                }
                CheckForSnapshot(snapshot);

                if (overrideChanged) {
                    levelChanged = true;
                    applyToMarkedFaces([&side](SegmentSide& dest) { dest.DynamicMultiplierOverride = side.DynamicMultiplierOverride; });
                }
            }

            ImGui::TreePop();
        }

        if (levelChanged) Events::LevelChanged();
        return snapshot;
    }

    bool SideUVs(SegmentSide& side) {
        bool changed = false;
        bool snapshot = false;

        if (ImGui::TableBeginTreeNode("UVs")) {
            auto addUVSlider = [&changed, &side, &snapshot](const char* label, int point) {
                bool highlight = point == (int)Editor::Selection.Point;

                if (Settings::Editor.SelectionMode == SelectionMode::Edge)
                    highlight |= point == ((int)Editor::Selection.Point + 1) % 4;

                if (highlight)
                    ImGui::PushStyleColor(ImGuiCol_Text, { 0, 1, 0, 1 });

                ImGui::TableRowLabel(label);

                if (highlight)
                    ImGui::PopStyleColor();

                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::DragFloat2(fmt::format("##{}", label).c_str(), &side.UVs[point].x, 0.01f);

                CheckForSnapshot(snapshot);
            };

            addUVSlider("UV 0", 0);
            addUVSlider("UV 1", 1);
            addUVSlider("UV 2", 2);
            addUVSlider("UV 3", 3);

            ImGui::TreePop();
        }

        if (changed) Events::LevelChanged();
        return snapshot;
    }

    bool WallTypeDropdown(Level& level, const char* label, WallType& value) {
        static const char* wallTypeLabels[] = {
            "None", "Destroyable", "Door", "Illusion", "Fly-Through", "Closed", "Wall Trigger", "Cloaked"
        };

        auto& seg = level.GetSegment(Editor::Selection.Tag());
        auto wallTypes = level.IsDescent1() ? 6 : 8;

        bool changed = false;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo(label, wallTypeLabels[(int)value])) {
            for (int i = 0; i < wallTypes; i++) {
                // Hide non-wall triggers for sides without connections. INVERSE FOR CONNECTIONS
                if (!seg.SideHasConnection(Editor::Selection.Side) &&
                    ((WallType)i != WallType::None && (WallType)i != WallType::WallTrigger))
                    continue;

                const bool isSelected = (uint8)value == i;
                if (ImGui::Selectable(wallTypeLabels[i], isSelected)) {
                    value = (WallType)i;
                    changed = true;
                    Events::LevelChanged();
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    const char* KEY_LABELS[] = { "None", "Blue", "Gold", "Red" };
    constexpr WallKey KEY_VALUES[] = { WallKey::None, WallKey::Blue, WallKey::Gold, WallKey::Red };

    bool KeyDropdown(WallKey& value) {
        int selection = [&value] {
            if ((int)value & (int)WallKey::Blue) return 1;
            if ((int)value & (int)WallKey::Gold) return 2;
            if ((int)value & (int)WallKey::Red) return 3;
            return 0;
        }();

        bool changed = false;

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Key", KEY_LABELS[selection])) {
            for (int i = 0; i < std::size(KEY_LABELS); i++) {
                const bool isSelected = selection == i;
                if (ImGui::Selectable(KEY_LABELS[i], isSelected)) {
                    value = KEY_VALUES[i];
                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    bool DoorClipDropdown(DClipID& id) {
        bool changed = false;

        auto label = std::to_string((int)id);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##segs", label.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < Resources::GameData.DoorClips.size(); i++) {
                if (i == 2) continue; // clip 2 is invalid and has no animation frames
                const bool isSelected = (int)id == i;
                auto itemLabel = std::to_string(i);
                auto& clip = Resources::GameData.DoorClips[i];
                TexturePreview(clip.Frames[0], { 32 * Shell::DpiScale, 32 * Shell::DpiScale });

                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                    changed = true;
                    id = (DClipID)i;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    void OnChangeDoorClip(Level& level, const Wall& wall) {
        SetTextureFromDoorClip(level, wall.Tag, wall.Clip);
        auto& clip = Resources::GetDoorClip(wall.Clip);
        Render::LoadTextureDynamic(clip.Frames[0]);
        Events::LevelChanged();
    }

    bool WallLightDropdown(Option<bool>& value) {
        static const char* labels[] = { "Default", "No", "Yes" };
        bool changed = false;

        int index = 0;
        if (value) index = *value ? 2 : 1;

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##wallLightDropdown", labels[index])) {
            for (int i = 0; i < std::size(labels); i++) {
                const bool isSelected = i == index;
                if (ImGui::Selectable(labels[i], isSelected)) {
                    if (i == 0) value.reset();
                    else value = (i == 2);
                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    void ChangeWallType(Level& level, Tag src, const WallType& wallType) {
        auto changeWall = [&](Tag tag) {
            auto wall = level.TryGetWall(tag);

            if (!wall && wallType != WallType::None) {
                // No wall on this side, add a new one
                AddWallHelper(level, tag, wallType);
            }

            if (wallType == WallType::None) {
                // Remove the wall when type changes to none
                auto wallId = level.TryGetWallID(tag);
                Editor::RemoveWall(level, wallId);

                if (Settings::Editor.EditBothWallSides) {
                    auto other = level.GetConnectedWall(tag);
                    Editor::RemoveWall(level, other);
                }

                wall = nullptr;
            }
            else if (wall) {
                if (wall->Type == wallType) return; // no change
                InitWall(level, *wall, wallType);

                if (Settings::Editor.EditBothWallSides) {
                    if (auto other = level.GetConnectedWall(*wall))
                        InitWall(level, *other, wallType);
                }
            }
        };

        changeWall(src);

        for (auto& marked : GetSelectedFaces())
            changeWall(marked);
    }

    // Returns true if any wall properties changed
    bool WallProperties(Level& level, WallID id) {
        auto wall = level.TryGetWall(id);
        auto tag = Editor::Selection.Tag();
        auto other = level.TryGetWall(level.GetConnectedWall(tag));
        bool open = ImGui::TableBeginTreeNode("Wall type");

        auto wallType = wall ? wall->Type : WallType::None;

        if (WallTypeDropdown(level, "##WallType", wallType)) {
            Editor::History.SnapshotSelection();
            ChangeWallType(level, tag, wallType);
            Editor::History.SnapshotLevel("Change Wall Type");

            // Wall might have been added or deleted on this side so fetch it again
            wall = level.TryGetWall(tag);
        }

        bool changed = false;

        if (open) {
            auto changeWallClip = [&level, &wall, &other, &changed] {
                OnChangeDoorClip(level, *wall);
                if (other && Settings::Editor.EditBothWallSides) {
                    other->Clip = wall->Clip;
                    OnChangeDoorClip(level, *other);
                }

                for (auto& markedId : GetSelectedWalls()) {
                    auto& markedWall = level.GetWall(markedId);
                    markedWall.Clip = wall->Clip;
                    OnChangeDoorClip(level, markedWall);

                    auto markedOther = level.TryGetWall(level.GetConnectedWall(markedId));
                    if (Settings::Editor.EditBothWallSides && markedOther && markedOther->Type == markedWall.Type) {
                        markedOther->Clip = wall->Clip;
                        OnChangeDoorClip(level, *markedOther);
                    }
                }

                changed = true;
            };

            if (wall) {
                ImGui::TableRowLabel("ID");
                ImGui::Text("%i", id);
                //ImGui::Text("%i Seg %i:%i", id, wall->Tag.Segment, wall->Tag.Side);

                ImGui::TableRowLabel("Edit both sides");
                ImGui::Checkbox("##bothsides", &Settings::Editor.EditBothWallSides);

                auto flagCheckbox = [&other, &level, &changed](const char* label, WallFlag flag, Wall* w) {
                    ImGui::TableRowLabel(label);
                    if (FlagCheckbox(fmt::format("##{}", label).c_str(), flag, w->Flags)) {
                        if (Settings::Editor.EditBothWallSides && other && other->Type == w->Type)
                            other->SetFlag(flag, w->HasFlag(flag));

                        for (auto& markedId : GetSelectedWalls()) {
                            auto& markedWall = level.GetWall(markedId);
                            markedWall.SetFlag(flag, w->HasFlag(flag));

                            auto markedOther = level.TryGetWall(level.GetConnectedWall(markedId));
                            if (Settings::Editor.EditBothWallSides && markedOther && markedOther->Type == markedWall.Type)
                                markedOther->SetFlag(flag, w->HasFlag(flag));
                        }

                        changed = true;
                    }
                    return false;
                };

                switch (wall->Type) {
                    case WallType::Destroyable:
                    {
                        ImGui::TableRowLabel("Clip");
                        if (DoorClipDropdown(wall->Clip))
                            changeWallClip();

                        auto& clip = Resources::GetDoorClip(wall->Clip);
                        TexturePreview(clip.Frames[0]);

                        ImGui::TableRowLabel("Hit points");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputFloat("##Hit points", &wall->HitPoints, 1, 10, "%.0f")) {
                            if (Settings::Editor.EditBothWallSides && other && other->Type == wall->Type)
                                other->HitPoints = wall->HitPoints;

                            for (auto& markedId : GetSelectedWalls()) {
                                auto markedWall = level.TryGetWall(markedId);
                                if (markedWall && markedWall->Type == WallType::Destroyable)
                                    markedWall->HitPoints = wall->HitPoints;

                                auto markedOther = level.TryGetWall(level.GetConnectedWall(markedId));
                                if (Settings::Editor.EditBothWallSides && markedOther && markedOther->Type == WallType::Destroyable)
                                    markedOther->HitPoints = wall->HitPoints;
                            }
                        }

                        CheckForSnapshot(changed);
                        //FlagCheckbox("Destroyed", WallFlag::Blasted, wall.flags); // Same as creating an illusionary wall on the final frame of a destroyable effect
                        break;
                    }

                    case WallType::Door:
                    {
                        ImGui::TableRowLabel("Clip");
                        if (DoorClipDropdown(wall->Clip))
                            changeWallClip();

                        auto& clip = Resources::GetDoorClip(wall->Clip);
                        TexturePreview(clip.Frames[0]);

                        ImGui::TableRowLabel("Key");
                        if (KeyDropdown(wall->Keys)) {
                            changed = true;
                            if (other && Settings::Editor.EditBothWallSides)
                                other->Keys = wall->Keys;

                            for (auto& markedId : GetSelectedWalls()) {
                                auto markedWall = level.TryGetWall(markedId);
                                if (markedWall && markedWall->Type == WallType::Door)
                                    markedWall->Keys = wall->Keys;

                                auto markedOther = level.TryGetWall(level.GetConnectedWall(markedId));
                                if (Settings::Editor.EditBothWallSides && markedOther && markedOther->Type == WallType::Door)
                                    markedOther->Keys = wall->Keys;
                            }
                        }

                        flagCheckbox("Opened", WallFlag::DoorOpened, wall);
                        flagCheckbox("Locked", WallFlag::DoorLocked, wall);
                        flagCheckbox("Auto Close", WallFlag::DoorAuto, wall);
                        if (!level.IsDescent1())
                            flagCheckbox("Buddy Proof", WallFlag::BuddyProof, wall);
                        break;
                    }

                    case WallType::Illusion:
                    {
                        //ImGui::TableRowLabelEx("Off", "Set the wall to start invisible.\nTrigger with 'illusion on' to make visible.");
                        flagCheckbox("Off", WallFlag::IllusionOff, wall);
                        break;
                    }

                    case WallType::Cloaked:
                    {
                        ImGui::TableRowLabel("Cloak");
                        auto cloakValue = wall->CloakValue() * 100;
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputFloat("##cloak", &cloakValue, Wall::CloakStep * 110, Wall::CloakStep * 500, "%.0f%%")) {
                            wall->CloakValue(cloakValue / 100);

                            if (Settings::Editor.EditBothWallSides && other && other->Type == wall->Type)
                                other->CloakValue(cloakValue / 100);

                            for (auto& markedId : GetSelectedWalls()) {
                                auto markedWall = level.TryGetWall(markedId);
                                if (markedWall && markedWall->Type == WallType::Cloaked)
                                    markedWall->CloakValue(cloakValue / 100);

                                auto markedOther = level.TryGetWall(level.GetConnectedWall(markedId));
                                if (Settings::Editor.EditBothWallSides && markedOther && markedOther->Type == WallType::Cloaked)
                                    markedOther->CloakValue(cloakValue / 100);
                            }

                            Events::LevelChanged();
                        }

                        CheckForSnapshot(changed);
                        break;
                    }
                }

                ImGui::TableRowLabel("Blocks Light");
                if (WallLightDropdown(wall->BlocksLight)) {
                    for (auto& wid : GetSelectedWalls()) {
                        if (auto w = level.TryGetWall(wid)) {
                            w->BlocksLight = wall->BlocksLight;
                            auto cw = level.GetConnectedWall(*w);
                            if (Settings::Editor.EditBothWallSides && cw)
                                cw->BlocksLight = wall->BlocksLight;
                        }
                    }

                    if (Settings::Editor.EditBothWallSides && other)
                        other->BlocksLight = wall->BlocksLight;

                    changed = true;
                }
            }
            else {
                ImGui::TextDisabled("No wall");
            }

            ImGui::TreePop();
        }

        return changed;
    }

    string TextureFlagToString(TextureFlag flags) {
        if ((int)flags == 0) return "None";

        string str;
        auto appendFlag = [&](TextureFlag flag, const string& name) {
            if (HasFlag(flags, flag)) {
                if (str.empty()) str = name;
                else str += ", " + name;
            }
        };

        appendFlag(TextureFlag::Volatile, "Volatile");
        appendFlag(TextureFlag::Water, "Water");
        appendFlag(TextureFlag::ForceField, "ForceField");
        appendFlag(TextureFlag::GoalBlue, "GoalBlue");
        appendFlag(TextureFlag::GoalRed, "GoalRed");
        appendFlag(TextureFlag::GoalHoard, "GoalHoard");
        return str;
    }

    void TextureProperties(const char* label, LevelTexID ltid, bool isOverlay) {
        bool open = ImGui::TableBeginTreeNode(label);
        auto& ti = Resources::GetTextureInfo(ltid);

        if (isOverlay && ltid == LevelTexID::Unset) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("None");
        }
        else {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", ti.Name.c_str());
        }

        if (isOverlay && ltid > LevelTexID(0)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
                Events::SelectTexture(LevelTexID::None, LevelTexID::Unset);
        }

        if (open) {
            ImGui::TableRowLabel("Level TexID");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%i", ltid);

            ImGui::TableRowLabel("TexID");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%i", ti.ID);

            ImGui::TableRowLabel("Size");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%i x %i", ti.Width, ti.Height);

            ImGui::TableRowLabel("Average Color");
            ImGui::AlignTextToFramePadding();
            ImGui::ColorButton("##color", { ti.AverageColor.x, ti.AverageColor.y, ti.AverageColor.z, 1 });

            ImGui::TableRowLabel("Transparent");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s %s", ti.Transparent ? "Yes" : "No", ti.SuperTransparent ? "(super)" : "");

            auto& lti = Resources::GetLevelTextureInfo(ltid);
            ImGui::TableRowLabel("Lighting");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%.2f", lti.Lighting);

            ImGui::TableRowLabel("Effect clip");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%i", lti.EffectClip);

            if (lti.EffectClip != EClipID::None) {
                auto& effect = Resources::GetEffectClip(lti.EffectClip);

                ImGui::TableRowLabel("Destroyed eclip");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%i", effect.DestroyedEClip);

                ImGui::TableRowLabel("Destroyed texture");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%i", effect.DestroyedTexture);
            }

            ImGui::TableRowLabel("Damage");
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%.1f", lti.Damage);

            // ImGui::TableRowLabel("Volatile");
            // auto isVolatile = (bool)(lti.Flags & TextureFlag::Volatile);
            // ImGui::Checkbox("Volatile", &isVolatile);

            ImGui::TableRowLabel("Flags");
            ImGui::AlignTextToFramePadding();
            ImGui::Text(TextureFlagToString(lti.Flags).c_str());

            ImGui::TreePop();
        }

        //if (ImGui::Button("Find usage"))
        //    Selection.SelectByTexture(_texture);

        //if (ImGui::Button("Export"))
        //    ExportBitmap(_texture);
    }

    // Updates the wall connected to this source
    void UpdateOtherWall(Level& level, Tag source) {
        if (!Settings::Editor.EditBothWallSides) return;

        // Update other wall if mode is enabled
        auto wall = level.TryGetWall(source);
        auto otherSide = level.GetConnectedSide(source);
        auto otherWall = level.TryGetWall(otherSide);

        if (wall && otherWall) {
            // Copy relevant values
            otherWall->Clip = wall->Clip;
            otherWall->Type = wall->Type;
            otherWall->HitPoints = wall->HitPoints;
            otherWall->Flags = wall->Flags;
            otherWall->Keys = wall->Keys;
            otherWall->cloak_value = wall->cloak_value;
            OnChangeDoorClip(level, *otherWall);
        }
    }

    bool TransformPosition(Level& level, const Segment& seg, Editor::SelectionMode mode) {
        bool changed = false;
        bool snapshot = false;
        auto speed = Settings::Editor.TranslationSnap > 0 ? Settings::Editor.TranslationSnap : 0.01f;

        auto addSlider = [&](const char* label, float& value) {
            ImGui::Text(label);
            ImGui::SameLine(30 * Shell::DpiScale);
            ImGui::SetNextItemWidth(-1);
            ImGui::PushID(label);
            changed |= ImGui::DragFloat("##xyz", &value, speed, MIN_FIX, MAX_FIX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            CheckForSnapshot(snapshot);
            ImGui::PopID();
        };

        switch (mode) {
            case SelectionMode::Segment:
            {
                ImGui::TableRowLabel("Segment position");
                auto center = seg.Center;
                auto original = center;

                addSlider("X", center.x);
                addSlider("Y", center.y);
                addSlider("Z", center.z);

                if (changed) {
                    auto delta = center - original;

                    for (int i = 0; i < 8; i++)
                        level.Vertices[seg.Indices[i]] += delta;
                }
                break;
            }

            case SelectionMode::Face:
            {
                ImGui::TableRowLabel("Face position");
                auto face = Face::FromSide(level, Editor::Selection.Tag());
                auto center = face.Center();
                auto original = center;

                addSlider("X", center.x);
                addSlider("Y", center.y);
                addSlider("Z", center.z);

                if (changed) {
                    auto delta = center - original;
                    for (int i = 0; i < 4; i++)
                        face.GetPoint(i) += delta;
                }

                break;
            }

            case SelectionMode::Edge:
            {
                ImGui::TableRowLabel("Edge position");
                auto face = Face::FromSide(level, Editor::Selection.Tag());
                auto center = face.GetEdgeMidpoint(Editor::Selection.Point);
                auto original = center;

                addSlider("X", center.x);
                addSlider("Y", center.y);
                addSlider("Z", center.z);

                if (changed) {
                    auto delta = center - original;
                    face.GetPoint(Editor::Selection.Point) += delta;
                    face.GetPoint(Editor::Selection.Point + 1) += delta;
                }

                break;
            }

            case SelectionMode::Point:
            {
                ImGui::TableRowLabel("Vertex position");
                auto face = Face::FromSide(level, Editor::Selection.Tag());
                auto& point = face.GetPoint(Editor::Selection.Point);

                addSlider("X", point.x);
                addSlider("Y", point.y);
                addSlider("Z", point.z);
                break;
            }
        }

        if (changed) {
            Game::Level.UpdateAllGeometricProps();
            Events::LevelChanged();
        }

        return snapshot;
    }

    void PropertyEditor::SegmentProperties() {
        auto& level = Game::Level;

        ImGui::TableRowLabel("Segment");
        if (SegmentDropdown(Selection.Segment))
            Editor::Selection.SetSelection(Selection.Segment);

        auto [seg, side] = level.GetSegmentAndSide(Selection.Tag());
        bool snapshot = false;

        ImGui::TableRowLabel("Segment type");
        auto segType = seg.Type;
        if (SegmentTypeDropdown(segType)) {
            if (segType == SegmentType::Matcen && !level.CanAddMatcen()) {
                ShowWarningMessage(L"Maximum number of matcens reached");
            }
            else {
                SetSegmentType(level, Selection.Tag(), segType);
                for (auto& marked : GetSelectedSegments())
                    SetSegmentType(level, { marked, Selection.Side }, segType);

                Editor::History.SnapshotLevel("Set segment type");
            }
        }

        if (seg.Type == SegmentType::Matcen)
            MatcenProperties(level, seg.Matcen, _matcenEditor);

        ImGui::TableRowLabel("Side");
        SideDropdown(Selection.Side);

        ImGui::TableRowLabel("Room");
        ImGui::Text("%i", seg.Room);

        {
            ImGui::TableRowLabel("Overlay angle");
            static constexpr std::array angles = { "0 deg", "90 deg", "180 deg", "270 deg" };
            auto rotation = std::clamp((int)side.OverlayRotation, 0, 3);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##overlay", &rotation, 0, 3, angles[rotation])) {
                side.OverlayRotation = (OverlayRotation)std::clamp(rotation, 0, 3);
                for (auto& tag : GetSelectedFaces()) {
                    if (auto markedSide = level.TryGetSide(tag))
                        markedSide->OverlayRotation = side.OverlayRotation;
                }
                Events::LevelChanged();
            }

            CheckForSnapshot(snapshot);
        }

        snapshot |= WallProperties(level, side.Wall);


        if (level.IsDescent1())
            snapshot |= TriggerPropertiesD1(level, side.Wall);
        else
            snapshot |= TriggerPropertiesD2(level, side.Wall);

        if (!level.IsDescent1())
            snapshot |= FlickeringProperties(level, Selection.Tag());

        {
            auto& connection = seg.Connections[(int)Selection.Side];
            DisableControls disableExit(connection > SegID::None);
            ImGui::TableRowLabel("End of exit tunnel");

            bool isExit = connection == SegID::Exit;
            if (ImGui::Checkbox("##endofexit", &isExit)) {
                connection = isExit ? SegID::Exit : SegID::None;
                snapshot = true;
            }
        }

        TextureProperties("Base texture", side.TMap, false);
        TextureProperties("Overlay texture", side.TMap2, true);
        snapshot |= SideLighting(level, seg, side);
        snapshot |= SideUVs(side);

        ImGui::TableRowLabel("Segment size");
        ImGui::Text("%.2f x %.2f x %.2f",
                    Vector3::Distance(seg.Sides[0].Center, seg.Sides[2].Center),
                    Vector3::Distance(seg.Sides[1].Center, seg.Sides[3].Center),
                    Vector3::Distance(seg.Sides[4].Center, seg.Sides[5].Center));

        auto face = Face::FromSide(level, Selection.Tag());
        if (Settings::Editor.SelectionMode == SelectionMode::Point || Settings::Editor.SelectionMode == SelectionMode::Edge) {
            ImGui::TableRowLabel("Edge length");
            ImGui::Text("%.2f", Vector3::Distance(face.GetPoint(Editor::Selection.Point), face.GetPoint(Editor::Selection.Point + 1)));
        }
        else {
            ImGui::TableRowLabel("Face Size");
            ImGui::Text("%.2f x %.2f",
                        Vector3::Distance(face.GetEdgeMidpoint(0), face.GetEdgeMidpoint(2)),
                        Vector3::Distance(face.GetEdgeMidpoint(1), face.GetEdgeMidpoint(3)));
        }

        snapshot |= TransformPosition(Game::Level, seg, Settings::Editor.SelectionMode);

        if (snapshot) {
            Events::LevelChanged();
            Editor::History.SnapshotSelection();
            Editor::History.SnapshotLevel("Change side");
        }
    }
}
