#include "pch.h"
#include "PropertyEditor.h"
#include "Game.h"
#include "../Editor.h"
#include "SoundSystem.h"

namespace Inferno::Editor {
    const char* GetObjectTypeName(ObjectType type) {
        if (type == ObjectType::None) return "None";

        constexpr std::array objectTypeLabels = {
            "None", // or "Wall"
            "Fireball",
            "Robot",
            "Hostage",
            "Player",
            "Mine", // Or weapon
            "Camera",
            "Powerup",
            "Debris",
            "Reactor",
            "Unused",
            "Clutter",
            "Ghost",
            "Light",
            "Player (Co-op)",
            "Marker"
        };

        if ((uint8)type >= objectTypeLabels.size()) return "Unknown";
        return objectTypeLabels[(int)type];
    }

    string GetObjectName(const Object& obj) {
        switch (obj.Type) {
            case ObjectType::Coop: return fmt::format("Coop player {}", obj.ID);
            case ObjectType::Player: return fmt::format("Player {}", obj.ID);
            case ObjectType::Hostage: return "Hostage";
            case ObjectType::Powerup:
            {
                if (auto name = Resources::GetPowerupName(obj.ID))
                    return name.value();
                else
                    return "Unknown powerup";
            }
            case ObjectType::Reactor: return "Reactor";
            case ObjectType::Robot: return Resources::GetRobotName(obj.ID);
            case ObjectType::Weapon: return "Mine";
            case ObjectType::SecretExitReturn: return "Secret exit return";
            default: return "Unknown object";
        }
    }

    bool ContainsDropdown(const char* label, ObjectType& containsType) {
        bool changed = false;

        if (ImGui::BeginCombo(label, GetObjectTypeName(containsType), ImGuiComboFlags_HeightLarge)) {
            static constexpr std::array availableTypes = {
                ObjectType::None,
                ObjectType::Robot,
                ObjectType::Powerup
            };

            for (auto type : availableTypes) {
                const bool isSelected = containsType == type;
                if (ImGui::Selectable(GetObjectTypeName(type), isSelected)) {
                    containsType = type;
                    changed = true;
                }

                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    constexpr int GetPowerupGroup(int id) {
        if (id == 1 || id == 2)
            return 0; // shields and energy to the top

        if (id >= 4 && id <= 6)
            return 1; // Keys

        if (id == 3 || (id >= 12 && id <= 16) || (id >= 28 && id <= 32))
            return 2; // primary weapons

        if (id == 10 || id == 11 || (id >= 17 && id <= 21) || (id >= 38 && id <= 45))
            return 3; // secondary weapons

        if (id == 46 || id == 47)
            return 11; // flags at the end

        return 10; // Everything else
    }

    struct PowerupSort {
        int8 ID;
        const Powerup* Ptr;
        string Name;
    };

    List<PowerupSort> SortPowerups() {
        auto powerupCount = Game::Level.IsDescent1() ? 26 : Resources::GameData.Powerups.size();
        List<PowerupSort> sorted;
        sorted.reserve(powerupCount);

        for (int8 i = 0; i < powerupCount; i++) {
            if (auto name = Resources::GetPowerupName(i)) {
                sorted.push_back({ i, &Resources::GameData.Powerups[i], *name });
            }
        }

        Seq::sortBy(sorted, [](PowerupSort& a, PowerupSort& b) {
            auto p0 = GetPowerupGroup(a.ID);
            auto p1 = GetPowerupGroup(b.ID);
            if (p0 < p1) return true;
            if (p1 < p0) return false;
            if (a.Name < b.Name) return true;
            if (b.Name < a.Name) return false;
            return false;
        });

        return sorted;
    }

    bool PowerupDropdown(const char* label, int8& id, Object* obj = nullptr) {
        auto name = Resources::GetPowerupName(id);
        auto preview = name.value_or("Unknown");
        bool changed = false;

        if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            auto sorted = SortPowerups();
            for (int i = 0; i < sorted.size(); i++) {
                const bool isSelected = id == sorted[i].ID;
                ImGui::PushID(i);
                if (ImGui::Selectable(sorted[i].Name.c_str(), isSelected)) {
                    id = sorted[i].ID;
                    if (obj) {
                        obj->Render.VClip.ID = sorted[i].Ptr->VClip;
                        obj->Radius = sorted[i].Ptr->Size;
                        Render::LoadTextureDynamic(obj->Render.VClip.ID);
                    }
                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    const std::map<AIBehavior, const char*> BehaviorLabels = {
        { AIBehavior::Normal, "Normal" },
        { AIBehavior::Still, "Still" },
        { AIBehavior::RunFrom, "Drop bombs" },
        { AIBehavior::Station, "Station" },
        { AIBehavior::Behind, "Get behind" },
        { AIBehavior::Snipe, "Snipe" },
        { AIBehavior::Follow, "Follow" },
        { (AIBehavior)231, "Drop smart bombs" }, // fake behavior id for UI
    };

    bool AIBehaviorDropdown(const char* label, RobotAI& ai) {
        bool changed = false;

        auto value = ai.Behavior;

        if (Game::Level.IsDescent2() && value == AIBehavior::RunFrom && ai.SmartMineFlag())
            value = (AIBehavior)231;

        if (!BehaviorLabels.contains(ai.Behavior))
            value = AIBehavior::Normal; // hack to prevent crash on invalid objects. This can occur after changing an object to a robot or custom DLE types.

        if (ImGui::BeginCombo(label, BehaviorLabels.at(value))) {
            for (auto& [item, text] : BehaviorLabels) {
                if ((int)item == 231 && Game::Level.IsDescent1()) continue; // No smart mines in D1

                const bool isSelected = item == value;
                if (ImGui::Selectable(text, isSelected)) {
                    if ((int)item == 231) {
                        ai.Behavior = AIBehavior::RunFrom;
                        ai.SmartMineFlag(true);
                    }
                    else {
                        ai.Behavior = item;
                        ai.SmartMineFlag(false);
                    }

                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    bool LevelTextureDropdown(const char* label, LevelTexID& current) {
        string currentLabel = current == LevelTexID::None ? "None" : Resources::GetTextureInfo(current).Name;
        bool changed = false;

        if (ImGui::BeginCombo(label, currentLabel.c_str(), ImGuiComboFlags_HeightLarge)) {
            {
                // Prepend the None case
                const bool isSelected = LevelTexID::None == current;
                if (ImGui::Selectable("None", isSelected)) {
                    current = LevelTexID::None;
                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            for (auto& lti : Resources::GameData.TexInfo) {
                auto& ti = Resources::GetTextureInfo(lti.ID);
                // Remove animated textures except for the base
                if (ti.Animated && ti.Frame != 0)
                    continue;

                //TexturePreview(lti.ID, { 32, 32 });
                //ImGui::SameLine();

                const bool isSelected = lti.ID == current;
                auto itemLabel = fmt::format("{}: {}", lti.ID, ti.Name);
                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                    current = lti.ID;
                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    struct RobotSort { int8 ID; string Name; };

    List<RobotSort> SortRobots() {
        auto robotCount = Game::Level.IsDescent1() ? 24 : Resources::GameData.Robots.size();
        List<RobotSort> sorted;
        sorted.reserve(robotCount);

        for (int8 i = 0; i < robotCount; i++) {
            sorted.push_back({ i, Resources::GetRobotName(i) });
        }

        Seq::sortBy(sorted, [](RobotSort& a, RobotSort& b) {
            if (a.Name < b.Name) return true;
            if (b.Name < a.Name) return false;
            return false;
        });

        return sorted;
    }

    bool RobotDropdown(const char* label, int8& id) {
        bool changed = false;

        if (ImGui::BeginCombo(label, Resources::GetRobotName(id).c_str(), ImGuiComboFlags_HeightLarge)) {
            auto sorted = SortRobots();

            for (int8 i = 0; i < sorted.size(); i++) {
                const bool isSelected = id == sorted[i].ID;
                if (ImGui::Selectable(sorted[i].Name.c_str(), isSelected)) {
                    id = (int8)sorted[i].ID;
                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    bool RobotProperties(Object& obj) {
        bool changed = false;

        ImGui::TableRowLabel("Robot");
        ImGui::SetNextItemWidth(-1);

        if (RobotDropdown("##Robot", obj.ID)) {
            const auto& robot = Resources::GameData.Robots[obj.ID];
            obj.Render.Model.ID = robot.Model;
            obj.Radius = GetObjectRadius(obj);
            obj.Physics.Mass = robot.Mass;
            obj.Physics.Drag = robot.Drag;

            ForMarkedObjects([&obj](Object& o) {
                if (o.Type != obj.Type) return;
            o.ID = obj.ID;
            o.Render.Model.ID = obj.Render.Model.ID;
            o.Radius = GetObjectRadius(obj);
            });

            Render::LoadModelDynamic(robot.Model);
            changed = true;
        }

        ImGui::TableRowLabel("Robot ID");
        ImGui::Text("%i", obj.ID);

        ImGui::TableRowLabel("Behavior");
        ImGui::SetNextItemWidth(-1);
        if (AIBehaviorDropdown("##Behavior", obj.Control.AI)) {
            ForMarkedObjects([&obj](Object& o) {
                if (o.Type != obj.Type) return;
            o.Control.AI.Behavior = obj.Control.AI.Behavior;
            o.Control.AI.Flags = obj.Control.AI.Flags;
            });
            changed = true;
        }

        ImGui::TableRowLabel("Contains");
        ImGui::SetNextItemWidth(-1);
        if (ContainsDropdown("##Contains", obj.Contains.Type)) {
            obj.Contains.ID = 0; // Reset to prevent out of range IDs

            if (obj.Contains.Type != ObjectType::None && obj.Contains.Count == 0)
                obj.Contains.Count = 1;
            else if (obj.Contains.Type == ObjectType::None)
                obj.Contains.Count = 0;

            ForMarkedObjects([&obj](Object& o) {
                if (o.Type != obj.Type) return;
            o.Contains.Type = obj.Contains.Type;
            o.Contains.Count = obj.Contains.Count;
            });
            changed = true;
        }

        bool containsChanged = false;

        if (obj.Contains.Type == ObjectType::Robot) {
            ImGui::TableRowLabel("Robot");
            ImGui::SetNextItemWidth(-1);
            containsChanged |= RobotDropdown("##RobotContains", obj.Contains.ID);
        }
        else if (obj.Contains.Type == ObjectType::Powerup) {
            ImGui::TableRowLabel("Object");
            ImGui::SetNextItemWidth(-1);
            containsChanged |= PowerupDropdown("##ObjectContains", obj.Contains.ID);
        }

        if (obj.Contains.Type == ObjectType::Robot || obj.Contains.Type == ObjectType::Powerup) {
            auto count = (int)obj.Contains.Count;
            ImGui::TableRowLabel("Count");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##Count", &count)) {
                obj.Contains.Count = (int8)std::clamp(count, 0, 100);
                containsChanged = true;
            }
        }

        if (containsChanged) {
            ForMarkedObjects([&obj](Object& o) {
                if (o.Type != obj.Type) return;
            o.Contains = obj.Contains;
            });
            changed = true;
        }

        if (ImGui::TableBeginTreeNode("Robot details")) {
            const auto& robot = Resources::GameData.Robots[obj.ID];
            ImGui::TableRowLabel("Hit points");
            ImGui::Text("%.2f (%.2f)", robot.HitPoints, obj.HitPoints);

            ImGui::TableRowLabel("Mass");
            ImGui::Text("%.2f", robot.Mass);

            //ImGui::TableRowLabel("Drag");
            //ImGui::Text("%.2f", robot.Drag);

            //ImGui::TableRowLabel("See sound");
            //if(ImGui::SmallButton("Play##see")) Sound::Play3D(robot.SeeSound, Selection.Object);

            //ImGui::TableRowLabel("Attack sound");
            //if (ImGui::SmallButton("Play##atk")) Sound::Play3D(robot.AttackSound, Selection.Object);

            //ImGui::TableRowLabel("Claw sound");
            //if (ImGui::SmallButton("Play##claw")) Sound::Play3D(robot.ClawSound, Selection.Object);

            //ImGui::TableRowLabel("Taunt sound");
            //if (ImGui::SmallButton("Play##taunt")) Sound::Play3D(robot.TauntSound, Selection.Object);

            //ImGui::TableRowLabel("Explosion 1");
            //if (ImGui::SmallButton("Play##exp1")) Sound::Play3D(robot.ExplosionSound1, Selection.Object);

            //ImGui::TableRowLabel("Explosion 2");
            //if (ImGui::SmallButton("Play##exp2")) Sound::Play3D(robot.ExplosionSound2, Selection.Object);

            //ImGui::TableRowLabel("Deathroll");
            //if (ImGui::SmallButton("Play##droll")) Sound::Play3D(robot.DeathrollSound, Selection.Object);

            ImGui::TreePop();
        }

        return changed;
    }

    bool ReactorModelDropdown(Object& obj) {
        bool changed = false;

        auto idStr = std::to_string(obj.ID);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##reactor", idStr.c_str())) {
            for (int8 i = 0; i < Resources::GameData.Reactors.size(); i++) {
                const bool isSelected = obj.ID == i;
                auto iStr = std::to_string(i);
                if (ImGui::Selectable(iStr.c_str(), isSelected)) {
                    obj.ID = i;
                    auto& reactor = Resources::GameData.Reactors[(int)obj.ID];
                    obj.Render.Model.ID = reactor.Model;
                    obj.Radius = GetObjectRadius(obj);
                    Render::LoadModelDynamic(reactor.Model);
                    changed = true;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    constexpr int GetObjectTypePriority(ObjectType t) {
        switch (t) {
            case ObjectType::Player: return 0;
            case ObjectType::Coop: return 1;
            case ObjectType::Powerup: return 2;
            case ObjectType::Hostage: return 3;
            case ObjectType::Robot: return 4;
            case ObjectType::Weapon: return 5;
            case ObjectType::Clutter: return 8;
            case ObjectType::Reactor: return 9;
            default: return 10;
        }
    }

    struct ObjectSort {
        ObjID ID;
        const Object* Obj;
        string Name;
    };

    List<ObjectSort> SortObjects(const List<Object>& objects) {
        List<ObjectSort> sorted;
        sorted.reserve(objects.size());

        for (int i = 0; i < objects.size(); i++)
            sorted.push_back({ (ObjID)i, &objects[i], GetObjectName(objects[i]) });

        Seq::sortBy(sorted, [](auto& a, auto& b) {
            auto p0 = GetObjectTypePriority(a.Obj->Type);
            auto p1 = GetObjectTypePriority(b.Obj->Type);
            if (p0 < p1) return true;
            if (p1 < p0) return false;
            if (a.Name < b.Name) return true;
            if (b.Name < a.Name) return false;
            return false;
        });

        return sorted;
    }

    bool ObjectDropdown(const Level& level, ObjID& id) {
        bool changed = false;
        //auto label = fmt::format("{}: {}", id, GetObjectName(level.Objects[(int)id]));
        auto label = GetObjectName(level.Objects[(int)id]);

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##objs", label.c_str(), ImGuiComboFlags_HeightLarge)) {
            auto sorted = SortObjects(level.Objects);
            for (int i = 0; i < sorted.size(); i++) {
                const bool isSelected = id == sorted[i].ID;
                ImGui::PushID(i);
                if (ImGui::Selectable(sorted[i].Name.c_str(), isSelected)) {
                    changed = true;
                    id = sorted[i].ID;
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    void TransformPosition(Object& obj) {
        bool changed = false;
        bool finishedEdit = false;
        auto speed = Settings::Editor.TranslationSnap > 0 ? Settings::Editor.TranslationSnap : 0.01f;
        auto angleSpeed = Settings::Editor.RotationSnap > 0 ? Settings::Editor.RotationSnap : DirectX::XM_PI / 32;

        auto Slider = [&](const char* label, float& value) {
            ImGui::Text(label); ImGui::SameLine(30 * Shell::DpiScale); ImGui::SetNextItemWidth(-1);
            ImGui::PushID(label);
            changed |= ImGui::DragFloat("##xyz", &value, speed, MIN_FIX, MAX_FIX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            finishedEdit |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopID();
        };

        auto AngleSlider = [&](const char* label, float& value) {
            ImGui::Text(label); ImGui::SameLine(60 * Shell::DpiScale); ImGui::SetNextItemWidth(-1);
            ImGui::PushID(label);
            changed |= ImGui::DragFloat("##pyr", &value, angleSpeed, MIN_FIX, MAX_FIX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            finishedEdit |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopID();
        };

        ImGui::TableRowLabel("Object position");
        Slider("X", obj.Position.x);
        Slider("Y", obj.Position.y);
        Slider("Z", obj.Position.z);

        //ImGui::TableRowLabel("Object rotation");
        //auto angles = Matrix(obj.Rotation).ToEuler(); 
        //AngleSlider("Pitch", angles.x);
        //AngleSlider("Yaw", angles.y);
        //AngleSlider("Roll", angles.z);

        if (changed) {
            //obj.Rotation = Matrix3x3(Matrix::CreateFromYawPitchRoll(angles));
            Editor::Gizmo.UpdatePosition();
        }

        if (finishedEdit) {
            Editor::History.SnapshotSelection();
            Editor::History.SnapshotLevel("Edit object position");
        }
    }

    void PropertyEditor::ObjectProperties() const {
        DisableControls disable(!Resources::HasGameData());

        ImGui::TableRowLabel("Object");
        if (ObjectDropdown(Game::Level, Selection.Object))
            Editor::Selection.SetSelection(Selection.Object);

        auto& obj = Game::Level.GetObject(Selection.Object);

        ImGui::TableRowLabel("Segment");
        if (SegmentDropdown(obj.Segment))
            Editor::History.SnapshotLevel("Change object segment");

        ImGui::TableRowLabel("Type");
        ImGui::SetNextItemWidth(-1);

        if (obj.Type == ObjectType::SecretExitReturn) {
            ImGui::Text("Secret Exit Return");
        }
        else if (ImGui::BeginCombo("##Type", GetObjectTypeName(obj.Type))) {
            static constexpr std::array availableTypes = {
                ObjectType::Robot,
                ObjectType::Powerup,
                ObjectType::Hostage,
                ObjectType::Player,
                ObjectType::Coop,
                ObjectType::Reactor,
                ObjectType::Weapon
            };

            auto typeCount = Game::Level.IsDescent1() ? availableTypes.size() - 1 : availableTypes.size();
            for (int i = 0; i < typeCount; i++) {
                auto type = availableTypes[i];
                const bool isSelected = obj.Type == type;
                if (ImGui::Selectable(GetObjectTypeName(type), isSelected)) {
                    InitObject(Game::Level, obj, type);
                    ForMarkedObjects([type](Object& o) { InitObject(Game::Level, o, type); });
                    Editor::History.SnapshotLevel("Change object type");
                }

                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        switch (obj.Type) {
            case ObjectType::Powerup:
                ImGui::TableRowLabel("Powerup");
                ImGui::SetNextItemWidth(-1);
                if (PowerupDropdown("##Powerup", obj.ID, &obj)) {
                    ForMarkedObjects([&obj](Object& o) {
                        if (o.Type != obj.Type) return;
                        o.Render.VClip.ID = obj.Render.VClip.ID;
                        o.Radius = obj.Radius;
                    });
                    Editor::History.SnapshotLevel("Change object");
                }

                break;

            case ObjectType::Robot:
                if (RobotProperties(obj))
                    Editor::History.SnapshotLevel("Change robot properties");
                break;

            case ObjectType::Reactor:
                ImGui::TableRowLabel("Model");
                if (ReactorModelDropdown(obj))
                    Editor::History.SnapshotLevel("Change reactor model");

                break;

            case ObjectType::Weapon: // mines
                ImGui::TableRowLabel("Angular velocity");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderFloat3("##angular", &obj.Physics.AngularVelocity.x, -1.57f, 1.57f, "%.2f")) {
                    ForMarkedObjects([&obj](Object& o) {
                        if (o.Type != obj.Type) return;
                    o.Physics.AngularVelocity = obj.Physics.AngularVelocity;
                    });
                }

                if (ImGui::IsItemDeactivatedAfterEdit())
                    Editor::History.SnapshotLevel("Change angular velocity");

                break;

            case ObjectType::Player:
            case ObjectType::Coop:
                ImGui::TableRowLabel("ID", "Saving the level sets the ID");
                ImGui::Text("%i", obj.ID);
                break;
        }

        if (obj.Render.Type == RenderType::Model && obj.Type != ObjectType::SecretExitReturn) {
            ImGui::TableRowLabel("Texture override");
            ImGui::SetNextItemWidth(-1);
            if (LevelTextureDropdown("##Texture", obj.Render.Model.TextureOverride)) {
                Render::LoadTextureDynamic(obj.Render.Model.TextureOverride);
                ForMarkedObjects([&obj](Object& o) {
                    if (o.Render.Type != obj.Render.Type) return;
                o.Render.Model.TextureOverride = obj.Render.Model.TextureOverride;
                });
                Editor::History.SnapshotLevel("Change object");
            }
            TexturePreview(obj.Render.Model.TextureOverride);

            ImGui::TableRowLabel("Polymodel");
            ImGui::Text("%i", obj.Render.Model.ID);
        }

        TransformPosition(obj);
    }
}