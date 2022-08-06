#include "pch.h"
#include "PropertyEditor.h"
#include "../Editor.h"

namespace Inferno::Editor {
    inline bool TriggerTypesDropdown(int& value) {
        static const char* TriggerTypeLabels[] = {
            "None", "Open Door", "Close Door", "Matcen", "Exit", "Secret Exit", "Illusion Off",
            "Illusion On", "Unlock Door", "Lock Door", "Open Wall", "Close Wall", "Illusory Wall",
            "Light Off", "Light On"
        };

        bool changed = false;

        if (ImGui::BeginCombo("##triggertype", TriggerTypeLabels[value], ImGuiComboFlags_HeightLarge)) {

            for (int i = 0; i < std::size(TriggerTypeLabels); i++) {
                const bool isSelected = i == value;
                if (ImGui::Selectable(TriggerTypeLabels[i], isSelected)) {
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

        if (ImGui::Button("Add##TriggerTarget", { 100 * Shell::DpiScale, 0 })) {
            if (Editor::Marked.Faces.empty())
                ShowWarningMessage(L"Please mark faces to add as targets.");

            for (auto& mark : Editor::Marked.Faces) {
                AddTriggerTarget(level, tid, mark);
                changed = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Remove##TriggerTarget", { 100 * Shell::DpiScale, 0 })) {
            RemoveTriggerTarget(level, tid, selectedIndex);
            if (selectedIndex > trigger.Targets.Count()) selectedIndex--;
            changed = true;
        }
        return changed;
    }

    bool TriggerPropertiesD1(Level& level, WallID wid) {
        bool changed = false;
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

                changed |= TriggerTargetsPicker(level, *trigger, wall->Trigger);

                ImGui::TableRowLabel("Open door");
                changed |= FlagCheckbox("##No Message", TriggerFlagD1::OpenDoor, trigger->FlagsD1);

                ImGui::TableRowLabel("Exit");
                changed |= FlagCheckbox("##Exit", TriggerFlagD1::Exit, trigger->FlagsD1);

                ImGui::TableRowLabel("Secret exit");
                changed |= FlagCheckbox("##Secret exit", TriggerFlagD1::SecretExit, trigger->FlagsD1);

                ImGui::TableRowLabel("Matcen");
                changed |= FlagCheckbox("##Matcen", TriggerFlagD1::Matcen, trigger->FlagsD1);

                ImGui::TableRowLabel("Illusion off");
                changed |= FlagCheckbox("##IllusionOff", TriggerFlagD1::IllusionOff, trigger->FlagsD1);

                ImGui::TableRowLabel("Illusion on");
                changed |= FlagCheckbox("##IllusionOn", TriggerFlagD1::IllusionOn, trigger->FlagsD1);
            }
            else {
                ImGui::TextDisabled("No trigger");
            }

            ImGui::TreePop();
        }

        return changed;
    }

    bool TriggerProperties(Level& level, WallID wallId) {
        bool changed = false;
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
                changed = true;
            }

            trigger = level.TryGetTrigger(wallId);
        }

        if (open) {
            if (trigger) {
                ImGui::TableRowLabel("ID");
                ImGui::Text("%i", tid);

                changed |= TriggerTargetsPicker(level, *trigger, tid);

                ImGui::TableRowLabel("No message");
                changed |= FlagCheckbox("##No Message", TriggerFlag::NoMessage, trigger->Flags);

                ImGui::TableRowLabel("One shot");
                changed |= FlagCheckbox("##One shot", TriggerFlag::OneShot, trigger->Flags);
            }
            else {
                ImGui::TextDisabled("No trigger");
            }

            ImGui::TreePop();
        }

        return changed;
    }

    void FlickeringProperties(Level& level, Tag tag) {
        auto light = level.GetFlickeringLight(tag);
        bool open = ImGui::TableBeginTreeNode("Flickering light");

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

                if (ImGui::Button("Shift Left", { 100 * Shell::DpiScale, 0 }))
                    light->ShiftLeft();

                ImGui::SameLine(0, 5);
                if (ImGui::Button("Shift Right", { 100 * Shell::DpiScale, 0 }))
                    light->ShiftRight();

                if (ImGui::Button("Defaults..."))
                    ImGui::OpenPopup("FlickerDefaults");

                ImGui::SetNextWindowSize({ 100 * Shell::DpiScale, -1 });
                if (ImGui::BeginPopup("FlickerDefaults")) {
                    if (ImGui::Selectable("On")) light->Mask = FlickeringLight::Defaults::On;
                    if (ImGui::Selectable("Off")) light->Mask = 0;
                    if (ImGui::Selectable("Strobe / 4")) light->Mask = FlickeringLight::Defaults::Strobe4;
                    if (ImGui::Selectable("Strobe / 8")) light->Mask = FlickeringLight::Defaults::Strobe8;
                    if (ImGui::Selectable("Flicker")) light->Mask = FlickeringLight::Defaults::Flicker;
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
    }

    bool SegmentTypeDropdown(SegmentType& type) {
        bool changed = false;

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##segtype", SegmentTypeLabels[(int)type])) {
            for (int i = 0; i < std::size(SegmentTypeLabels); i++) {
                if (i == 2) continue;

                const bool isSelected = (int)type == i;
                if (ImGui::Selectable(SegmentTypeLabels[i], isSelected)) {
                    changed = true;
                    type = (SegmentType)i;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    string GetMatcenRobotLabel(Level& level, const Matcen& matcen) {
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

    bool SideLighting(Level& level, Segment& seg, SegmentSide& side) {
        bool open = ImGui::TableBeginTreeNode("Light override");
        bool changed = false;

        if (open) {
            {
                // Emission override
                bool overrideChanged = false;
                bool hasOverride = side.LightOverride.has_value();
                auto light = side.LightOverride.value_or(GetLightColor(side));

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Emission", &hasOverride)) {
                    side.LightOverride = hasOverride ? Option<Color>(light) : std::nullopt;
                    overrideChanged = true;
                }

                ImGui::TableNextColumn();
                DisableControls disable(!hasOverride);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::ColorEdit3("##customcolor", &light.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)) {
                    side.LightOverride = light;
                    overrideChanged = true;
                }

                if (overrideChanged) {
                    // Also update marked faces
                    for (auto& tag : GetSelectedFaces()) {
                        if (auto marked = level.TryGetSide(tag))
                            marked->LightOverride = side.LightOverride;
                    }
                }
            }

            {
                // Radius override
                bool overrideChanged = false;
                bool hasOverride = side.LightRadiusOverride.has_value();
                auto radius = side.LightRadiusOverride.value_or(Settings::Lighting.Radius);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Radius", &hasOverride)) {
                    side.LightRadiusOverride = hasOverride ? Option<float>(radius) : std::nullopt;
                    overrideChanged = true;
                }

                ImGui::TableNextColumn();
                DisableControls disable(!hasOverride);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##radius", &radius, 0, 30, "%.1f")) {
                    side.LightRadiusOverride = radius;
                    overrideChanged = true;
                }

                if (overrideChanged) {
                    // Also update marked faces
                    for (auto& tag : GetSelectedFaces()) {
                        if (auto marked = level.TryGetSide(tag))
                            marked->LightRadiusOverride = side.LightRadiusOverride;
                    }
                }
            }

            {
                // Light plane override
                bool overrideChanged = false;
                bool hasOverride = side.LightPlaneOverride.has_value();
                auto plane = side.LightPlaneOverride.value_or(Settings::Lighting.LightPlaneTolerance);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Light plane", &hasOverride)) {
                    side.LightPlaneOverride = hasOverride ? Option<float>(plane) : std::nullopt;
                    overrideChanged = true;
                }

                ImGui::TableNextColumn();
                DisableControls disable(!hasOverride);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##lightplane", &plane, -0.01f, -1)) {
                    side.LightPlaneOverride = plane;
                    overrideChanged = true;
                }

                if (overrideChanged) {
                    // Also update marked faces
                    for (auto& tag : GetSelectedFaces()) {
                        if (auto marked = level.TryGetSide(tag))
                            marked->LightPlaneOverride = side.LightPlaneOverride;
                    }
                }
            }

            {
                // Occlusion
                ImGui::TableRowLabel("Occlusion");
                if (ImGui::Checkbox("##Occlusion", &side.EnableOcclusion)) {
                    for (auto& tag : GetSelectedFaces()) {
                        if (auto marked = level.TryGetSide(tag))
                            marked->EnableOcclusion = side.EnableOcclusion;
                    }
                }
            }

            auto VertexLightSlider = [&changed, &side](const char* label, int point) {
                if (point == (int)Editor::Selection.Point)
                    ImGui::PushStyleColor(ImGuiCol_Text, { 0, 1, 0, 1 });

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Checkbox(label, &side.LockLight[point]);

                if (point == (int)Editor::Selection.Point)
                    ImGui::PopStyleColor();

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                DisableControls disable(!side.LockLight[point]);
                if (ImGui::ColorEdit3(fmt::format("##{}", label).c_str(), &side.Light[point].x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float)) {
                    changed = true;
                }
            };

            VertexLightSlider("Point 0", 0);
            VertexLightSlider("Point 1", 1);
            VertexLightSlider("Point 2", 2);
            VertexLightSlider("Point 3", 3);

            {
                // Volume light
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Checkbox("Volume", &seg.LockVolumeLight);

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                DisableControls disable(!seg.LockVolumeLight);
                if (ImGui::ColorEdit3("##volume", &seg.VolumeLight.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float))
                    changed = true;
            }

            {
                // Dynamic multiplier
                bool overrideChanged = false;
                bool hasOverride = side.DynamicMultiplierOverride.has_value();
                auto mult = side.DynamicMultiplierOverride.value_or(1);

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Checkbox("Dynamic multiplier", &hasOverride)) {
                    side.DynamicMultiplierOverride = hasOverride ? Option<float>(mult) : std::nullopt;
                    overrideChanged = true;
                }

                ImGui::TableNextColumn();
                DisableControls disable(!hasOverride);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat("##dynmult", &mult, 0, 1)) {
                    side.DynamicMultiplierOverride = mult;
                    overrideChanged = true;
                }

                if (overrideChanged) {
                    // Also update marked faces
                    for (auto& tag : GetSelectedFaces()) {
                        if (auto marked = level.TryGetSide(tag))
                            marked->DynamicMultiplierOverride = side.DynamicMultiplierOverride;
                    }
                }
            }

            ImGui::TreePop();
        }

        return changed;
    }

    bool SideUVs(SegmentSide& side) {
        bool changed = false;

        if (ImGui::TableBeginTreeNode("UVs")) {
            ImGui::TableRowLabel("UV 0");
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::DragFloat2("##P0", &side.UVs[0].x, 0.01f);

            ImGui::TableRowLabel("UV 1");
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::DragFloat2("##P1", &side.UVs[1].x, 0.01f);

            ImGui::TableRowLabel("UV 2");
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::DragFloat2("##P2", &side.UVs[2].x, 0.01f);

            ImGui::TableRowLabel("UV 3");
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::DragFloat2("##P3", &side.UVs[3].x, 0.01f);
            ImGui::TreePop();
        }

        return changed;
    }

    bool WallTypeDropdown(Level& level, const char* label, WallType& value) {
        static const char* WallTypeLabels[] = {
            "None", "Destroyable", "Door", "Illusion", "Fly-Through", "Closed", "Wall Trigger", "Cloaked"
        };

        auto& seg = level.GetSegment(Editor::Selection.Tag());
        auto wallTypes = level.IsDescent1() ? 6 : 8;

        bool changed = false;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo(label, WallTypeLabels[(int)value])) {
            for (int i = 0; i < wallTypes; i++) {
                // Hide non-wall triggers for sides without connections. INVERSE FOR CONNECTIONS
                if (!seg.SideHasConnection(Editor::Selection.Side) &&
                    ((WallType)i != WallType::None && (WallType)i != WallType::WallTrigger))
                    continue;

                const bool isSelected = (uint8)value == i;
                if (ImGui::Selectable(WallTypeLabels[i], isSelected)) {
                    value = (WallType)i;
                    changed = true;
                    Events::LevelChanged(); // Fly-through can affect rendering
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    bool KeyDropdown(WallKey& value) {
        static const char* KeyLabels[] = { "None", "Blue", "Gold", "Red" };
        static const WallKey KeyValues[] = { WallKey::None, WallKey::Blue, WallKey::Gold, WallKey::Red };

        int selection = [&value] {
            if ((int)value & (int)WallKey::Blue) return 1;
            if ((int)value & (int)WallKey::Gold) return 2;
            if ((int)value & (int)WallKey::Red) return 3;
            return 0;
        }();

        bool changed = false;

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Key", KeyLabels[(int)selection])) {
            for (int i = 0; i < std::size(KeyLabels); i++) {
                const bool isSelected = selection == i;
                if (ImGui::Selectable(KeyLabels[i], isSelected)) {
                    value = KeyValues[i];
                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    bool WallClipDropdown(WClipID& id) {
        bool changed = false;

        auto label = std::to_string((int)id);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##segs", label.c_str(), ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < Resources::GameData.WallClips.size(); i++) {
                if (i == 2) continue; // clip 2 is invalid and has no animation frames
                const bool isSelected = (int)id == i;
                auto itemLabel = std::to_string((int)i);
                auto& clip = Resources::GameData.WallClips[i];
                TexturePreview(clip.Frames[0], { 32 * Shell::DpiScale, 32 * Shell::DpiScale });

                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                    changed = true;
                    id = (WClipID)i;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    void OnChangeWallClip(Level& level, const Wall& wall) {
        SetTextureFromWallClip(level, wall.Tag, wall.Clip);
        if (auto clip = Resources::TryGetWallClip(wall.Clip)) {
            Render::LoadTextureDynamic(clip->Frames[0]);
            Events::LevelChanged();
        }
    }

    bool WallLightDropdown(Option<bool>& value) {
        static const char* Labels[] = { "Default", "No", "Yes" };
        bool changed = false;

        int index = 0;
        if (value) index = *value ? 2 : 1;

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##wallLightDropdown", Labels[index])) {
            for (int i = 0; i < std::size(Labels); i++) {
                const bool isSelected = i == index;
                if (ImGui::Selectable(Labels[i], isSelected)) {
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

    void ChangeWallType(Wall* wall, WallType type) {
        if (!wall) return;
        wall->Type = type;
        if (type == WallType::Cloaked)
            wall->CloakValue(0.5f);
    }

    // Returns true if any wall properties changed
    void WallProperties(Level& level, WallID id) {
        auto wall = level.TryGetWall(id);
        auto other = level.TryGetWall(level.GetConnectedWall(Editor::Selection.Tag()));
        bool open = ImGui::TableBeginTreeNode("Wall type");

        auto wallType = wall ? wall->Type : WallType::None;

        if (WallTypeDropdown(level, "##WallType", wallType)) {
            if (!wall && wallType != WallType::None) {
                Commands::AddWallType(wallType);
            }
            else {
                if (wallType == WallType::None) {
                    Commands::RemoveWall();
                }
                else {
                    ChangeWallType(wall, wallType);

                    if (Settings::EditBothWallSides)
                        ChangeWallType(other, wallType);

                    // Change type of marked faces if they already have a wall
                    for (auto& face : GetSelectedFaces())
                        if (auto markedWall = Game::Level.TryGetWall(face))
                            ChangeWallType(markedWall, wallType);

                    Editor::History.SnapshotLevel("Change wall type");
                }
            }

            // Wall might have been added or deleted so fetch it again
            wall = level.TryGetWall(Editor::Selection.Tag());
        }

        if (open) {
            if (wall) {
                ImGui::TableRowLabel("ID");
                ImGui::Text("%i", id);
                //ImGui::Text("%i Seg %i:%i", id, wall->Tag.Segment, wall->Tag.Side);

                ImGui::TableRowLabel("Edit both sides");
                ImGui::Checkbox("##bothsides", &Settings::EditBothWallSides);

                auto flagCheckbox = [&other](const char* label, WallFlag flag, Wall* wall) {
                    ImGui::TableRowLabel(label);
                    if (FlagCheckbox(fmt::format("##{}", label).c_str(), flag, wall->Flags)) {
                        if (Settings::EditBothWallSides && other && other->Type == wall->Type)
                            other->SetFlag(flag, wall->HasFlag(flag));
                    }
                };

                switch (wall->Type) {
                    case WallType::Destroyable:
                        ImGui::TableRowLabel("Clip");
                        if (WallClipDropdown(wall->Clip)) {
                            OnChangeWallClip(level, *wall);
                            if (other && Settings::EditBothWallSides) {
                                other->Clip = wall->Clip;
                                OnChangeWallClip(level, *other);
                            }
                        }

                        if (auto clip = Resources::TryGetWallClip(wall->Clip))
                            TexturePreview(clip->Frames[0]);

                        ImGui::TableRowLabel("Hit points");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputFloat("##Hit points", &wall->HitPoints, 1, 10, "%.0f")) {
                            if (Settings::EditBothWallSides && other && other->Type == wall->Type)
                                other->HitPoints = wall->HitPoints;
                        }

                        //FlagCheckbox("Destroyed", WallFlag::Blasted, wall.flags); // Same as creating an illusionary wall on the final frame of a destroyable effect
                        break;

                    case WallType::Door:
                    {
                        ImGui::TableRowLabel("Clip");
                        if (WallClipDropdown(wall->Clip)) {
                            OnChangeWallClip(level, *wall);
                            if (other && Settings::EditBothWallSides) {
                                other->Clip = wall->Clip;
                                OnChangeWallClip(level, *other);
                            }
                        }

                        if (auto clip = Resources::TryGetWallClip(wall->Clip))
                            TexturePreview(clip->Frames[0]);

                        ImGui::TableRowLabel("Key");
                        if (KeyDropdown(wall->Keys))
                            if (other && Settings::EditBothWallSides)
                                other->Keys = wall->Keys;

                        flagCheckbox("Opened", WallFlag::DoorOpened, wall);
                        flagCheckbox("Locked", WallFlag::DoorLocked, wall);
                        flagCheckbox("Auto Close", WallFlag::DoorAuto, wall);
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

                            if (Settings::EditBothWallSides && other && other->Type == wall->Type)
                                other->CloakValue(cloakValue / 100);

                            Events::LevelChanged();
                        }

                        break;
                    }
                }

                ImGui::TableRowLabel("Blocks Light");
                if (WallLightDropdown(wall->BlocksLight)) {
                    for (auto& wid : GetSelectedWalls())
                        if (auto w = level.TryGetWall(wid))
                            w->BlocksLight = wall->BlocksLight;

                    if (Settings::EditBothWallSides && other)
                        other->BlocksLight = wall->BlocksLight;
                }
            }
            else {
                ImGui::TextDisabled("No wall");
            }

            ImGui::TreePop();
        }
    }

    string TextureFlagToString(TextureFlag flags) {
        if ((int)flags == 0) return "None";

        string str;
        auto AppendFlag = [&](TextureFlag flag, string name) {
            if (bool((ubyte)flags & (ubyte)flag)) {
                if (str.empty()) str = name;
                else str += ", " + name;
            }
        };

        AppendFlag(TextureFlag::Volatile, "Volatile");
        AppendFlag(TextureFlag::Water, "Water");
        AppendFlag(TextureFlag::ForceField, "ForceField");
        AppendFlag(TextureFlag::GoalBlue, "GoalBlue");
        AppendFlag(TextureFlag::GoalRed, "GoalRed");
        AppendFlag(TextureFlag::GoalHoard, "GoalHoard");
        return str;
    }

    void TextureProperties(const char* label, LevelTexID ltid, bool isOverlay) {
        bool open = ImGui::TableBeginTreeNode(label);
        const auto ti = Resources::TryGetTextureInfo(ltid);

        if (isOverlay && ltid == LevelTexID::Unset) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("None");
        }
        else if (ti) {
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", ti ? ti->Name.c_str() : "None");
        }

        if (isOverlay && ltid > LevelTexID(0)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear"))
                Events::SelectTexture(LevelTexID::None, LevelTexID::Unset);
        }

        if (open) {
            if (ti) {
                ImGui::TableRowLabel("Level TexID");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%i", ltid);

                ImGui::TableRowLabel("TexID");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%i", ti->ID);

                //ImGui::TableRowLabel("Size");
                //ImGui::AlignTextToFramePadding();
                //ImGui::Text("%i x %i", ti->Width, ti->Height);

                ImGui::TableRowLabel("Average Color");
                ImGui::AlignTextToFramePadding();
                ImGui::ColorButton("##color", { ti->AverageColor.x, ti->AverageColor.y, ti->AverageColor.z, 1 });

                ImGui::TableRowLabel("Transparent");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s %s", ti->Transparent ? "Yes" : "No", ti->SuperTransparent ? "(super)" : "");
            }

            if (auto lti = Resources::TryGetLevelTextureInfo(ltid)) {
                ImGui::TableRowLabel("Lighting");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%.2f", lti->Lighting);

                ImGui::TableRowLabel("Effect clip");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%i", lti->EffectClip);

                ImGui::TableRowLabel("Damage");
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%.1f", lti->Damage);

                // ImGui::TableRowLabel("Volatile");
                // auto isVolatile = (bool)(lti->Flags & TextureFlag::Volatile);
                // ImGui::Checkbox("Volatile", &isVolatile);

                ImGui::TableRowLabel("Flags");
                ImGui::AlignTextToFramePadding();
                ImGui::Text(TextureFlagToString(lti->Flags).c_str());
            }

            ImGui::TreePop();
        }

        //if (ImGui::Button("Find usage"))
        //    Selection.SelectByTexture(_texture);

        //if (ImGui::Button("Export"))
        //    ExportBitmap(_texture);
    }

    // Updates the wall connected to this source
    void UpdateOtherWall(Level& level, Tag source) {
        if (!Settings::EditBothWallSides) return;

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
            OnChangeWallClip(level, *otherWall);
        }
    }

    void PropertyEditor::SegmentProperties() {
        auto& level = Game::Level;

        ImGui::TableRowLabel("Segment");
        if (SegmentDropdown(Selection.Segment))
            Editor::Selection.SetSelection(Selection.Segment);

        auto [seg, side] = level.GetSegmentAndSide(Selection.Tag());
        bool changed = false;

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

        {
            ImGui::TableRowLabel("Overlay angle");
            static const std::array angles = { "0 deg", "90 deg", "180 deg", "270 deg" };
            auto rotation = std::clamp((int)side.OverlayRotation, 0, 3);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##overlay", &rotation, 0, 3, angles[rotation])) {
                side.OverlayRotation = (OverlayRotation)std::clamp(rotation, 0, 3);
                for (auto& tag : GetSelectedFaces()) {
                    if (auto markedSide = level.TryGetSide(tag))
                        markedSide->OverlayRotation = side.OverlayRotation;
                }
                Editor::History.SnapshotLevel("Change overlay angle");
                Events::LevelChanged();
            }
        }

        WallProperties(level, side.Wall);

        bool triggerChanged = false;
        if (level.IsDescent1())
            triggerChanged |= TriggerPropertiesD1(level, side.Wall);
        else
            triggerChanged |= TriggerProperties(level, side.Wall);

        if (triggerChanged) {
            Editor::History.SnapshotSelection();
            Editor::History.SnapshotLevel("Change Trigger");
        }

        if (!level.IsDescent1()) {
            FlickeringProperties(level, Selection.Tag());
        }

        {
            auto& connection = seg.Connections[(int)Selection.Side];
            DisableControls disableExit(connection > SegID::None);
            ImGui::TableRowLabel("End of exit tunnel");

            bool isExit = connection == SegID::Exit;
            if (ImGui::Checkbox("##endofexit", &isExit)) {
                connection = isExit ? SegID::Exit : SegID::None;
                changed = true;
            }
        }

        TextureProperties("Base Texture", side.TMap, false);
        TextureProperties("Overlay Texture", side.TMap2, true);
        changed |= SideLighting(level, seg, side);
        changed |= SideUVs(side);

        if (changed) {
            Events::LevelChanged();
        }
    }
}
