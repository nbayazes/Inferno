#include "pch.h"
#include "Game.AI.h"
#include "PropertyEditor.h"
#include "Game.h"
#include "Game.Object.h"
#include "../Editor.h"
#include "SoundSystem.h"
#include "Editor/Editor.Object.h"
#include "Editor/Gizmo.h"
#include "Graphics/Render.h"

namespace Inferno::Editor {
    const char* GetObjectTypeName(ObjectType type) {
        if (type == ObjectType::None) return "None";

        static constexpr std::array objectTypeLabels = {
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

        Seq::sortBy(sorted, [](const PowerupSort& a, const PowerupSort& b) {
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

    bool PowerupDropdown(const char* label, int8& id, const Object* obj = nullptr) {
        auto name = Resources::GetPowerupName(id);
        auto preview = name.value_or("Unknown");
        bool changed = false;

        if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
            auto sorted = SortPowerups();
            for (int i = 0; i < sorted.size(); i++) {
                auto& powerup = sorted[i];
                const bool isSelected = id == powerup.ID;
                ImGui::PushID(i);
                if (ImGui::Selectable(powerup.Name.c_str(), isSelected)) {
                    id = sorted[i].ID;
                    if (obj) {
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

    // fake behavior id for UI
    constexpr AIBehavior DROP_SMART_BOMBS = AIBehavior{ 231 };

    struct BehaviorLabel {
        AIBehavior behavior;
        const char* label;
    };

    constexpr std::array<BehaviorLabel, 6> BEHAVIOR_LABELS_D1 = {
        {
            { AIBehavior::Normal, "Normal" },
            { AIBehavior::Still, "Still" },
            { AIBehavior::RunFrom, "Drop bombs" },
            { AIBehavior::Station, "Station" },
            { AIBehavior::Hide, "Hide (unused)" },
            { AIBehavior::FollowPathD1, "Follow path (unused)" }
        },
    };

    constexpr std::array<BehaviorLabel, 8> BEHAVIOR_LABELS_D2 = {
        {
            { AIBehavior::Normal, "Normal" },
            { AIBehavior::Still, "Still" },
            { AIBehavior::RunFrom, "Drop bombs" },
            { DROP_SMART_BOMBS, "Drop smart bombs" },
            { AIBehavior::Snipe, "Snipe" },
            { AIBehavior::GetBehind, "Get behind" },
            { AIBehavior::Station, "Station (not impl)" },
            { AIBehavior::FollowPathD2, "Follow path (unused)" },
        }
    };

    bool AIBehaviorDropdown(const char* label, RobotAI& ai) {
        bool changed = false;

        auto value = ai.Behavior;

        if (Game::Level.IsDescent2() && value == AIBehavior::RunFrom && ai.SmartMineFlag())
            value = DROP_SMART_BOMBS;

        auto labels = Game::Level.IsDescent2()
            ? span<const BehaviorLabel>{ BEHAVIOR_LABELS_D2 }
            : span<const BehaviorLabel>{ BEHAVIOR_LABELS_D1 };

        const auto behaviorSearch = [&value](const BehaviorLabel& entry) { return entry.behavior == value; };

        auto entry = Seq::find(labels, behaviorSearch);
        if (!entry) {
            value = AIBehavior::Normal; // prevent crash on invalid objects. This can occur after changing an object to a robot or custom DLE types.
            entry = Seq::find(labels, behaviorSearch);
        }

        if (!entry) return false;

        if (ImGui::BeginCombo(label, entry->label)) {
            for (auto& [item, text] : labels) {
                const bool isSelected = item == value;
                if (ImGui::Selectable(text, isSelected)) {
                    if ((int)item == (int)DROP_SMART_BOMBS) {
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

            for (auto& lti : Resources::GameData.LevelTextures) {
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

    struct RobotSort {
        int8 ID;
        string Name;
    };

    List<RobotSort> SortRobots() {
        auto robotCount = Game::Level.IsDescent1() ? 24 : Resources::GameData.Robots.size();
        List<RobotSort> sorted;
        sorted.reserve(robotCount);

        for (int8 i = 0; i < robotCount; i++) {
            sorted.push_back({ i, Resources::GetRobotName(i) });
        }

        Seq::sortBy(sorted, [](const RobotSort& a, const RobotSort& b) {
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
                    id = sorted[i].ID;
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
                o.Physics.Mass = obj.Physics.Mass;
                o.Physics.Drag = obj.Physics.Drag;
            });

            Render::LoadModelDynamic(robot.Model);
            changed = true;
        }

        ImGui::TableRowLabel("Robot ID");
        ImGui::Text("%i:%i", obj.ID, obj.Signature);

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

        const auto& robot = Resources::GameData.Robots[obj.ID];

        if (ImGui::TableBeginTreeNode("Robot details")) {
            ImGui::TableRowLabel("Hit points");
            ImGui::Text("%.2f (%.2f)", robot.HitPoints, obj.HitPoints);

            ImGui::TableRowLabel("Mass");
            ImGui::Text("%.2f", robot.Mass);

            ImGui::TableRowLabel("Drag");
            ImGui::Text("%.2f", robot.Drag);
            ImGui::TreePop();
        }

        int imId = 0;
        if (ImGui::TableBeginTreeNode("Robot sounds")) {
            auto soundRow = [&imId](const char* label, SoundID id) {
                ImGui::PushID(imId++);
                ImGui::TableRowLabel(label);
                if (ImGui::Button(Resources::GetSoundName(id).data(), { -1, 0 }))
                    Sound::Play2D({ id });
                ImGui::PopID();
            };

            soundRow("See", robot.SeeSound);
            soundRow("Attack", robot.AttackSound);
            soundRow("Claw", robot.ClawSound);
            soundRow("Taunt", robot.TauntSound);
            soundRow("Explosion 1", robot.ExplosionSound1);
            soundRow("Explosion 2", robot.ExplosionSound2);
            soundRow("Deathroll", robot.DeathRollSound);
            ImGui::TreePop();
        }

        if (ImGui::TableBeginTreeNode("Robot animations")) {
            auto animationRow = [&imId, &obj](const char* label, Animation state) {
                ImGui::PushID(imId++);
                ImGui::TableRowLabel(label);
                if (ImGui::Button(label, { -1, 0 }))
                    PlayRobotAnimation(obj, state);
                ImGui::PopID();
            };

            animationRow("Rest", Animation::Rest);
            animationRow("Fire", Animation::Fire);
            animationRow("Flinch", Animation::Flinch);
            animationRow("Recoil", Animation::Recoil);
            animationRow("Alert", Animation::Alert);
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
                    InitObject(obj, obj.Type, obj.ID);
                    Render::LoadModelDynamic(obj.Render.Model.ID);
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
        //auto angleSpeed = Settings::Editor.RotationSnap > 0 ? Settings::Editor.RotationSnap : DirectX::XM_PI / 32;

        auto slider = [&](const char* label, float& value) {
            ImGui::Text(label);
            ImGui::SameLine(30 * Shell::DpiScale);
            ImGui::SetNextItemWidth(-1);
            ImGui::PushID(label);
            changed |= ImGui::DragFloat("##xyz", &value, speed, MIN_FIX, MAX_FIX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            finishedEdit |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopID();
        };

        //auto AngleSlider = [&](const char* label, float& value) {
        //    ImGui::Text(label);
        //    ImGui::SameLine(60 * Shell::DpiScale);
        //    ImGui::SetNextItemWidth(-1);
        //    ImGui::PushID(label);
        //    changed |= ImGui::DragFloat("##pyr", &value, angleSpeed, MIN_FIX, MAX_FIX, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        //    finishedEdit |= ImGui::IsItemDeactivatedAfterEdit();
        //    ImGui::PopID();
        //};

        ImGui::TableRowLabel("Object position");
        slider("X", obj.Position.x);
        slider("Y", obj.Position.y);
        slider("Z", obj.Position.z);

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

    void RandomizeMineRotation(Object& obj) {
        obj.Physics.AngularVelocity.y = (Random() - Random()) * 1.25f; // value between -1.25 and 1.25
    }

    void PropertyEditor::ObjectProperties() const {
        DisableControls disable(!Resources::HasGameData());

        auto label = fmt::format("Object {}", (int)Editor::Selection.Object);
        ImGui::TableRowLabel(label.c_str());
        if (ObjectDropdown(Game::Level, Selection.Object))
            Editor::Selection.SetSelection(Selection.Object);

        ImGui::TableRowLabel("Segment");

        auto pObj = Game::Level.TryGetObject(Selection.Object);
        if (!pObj) return;
        auto& obj = *pObj;

        if (SegmentDropdown(obj.Segment))
            Editor::History.SnapshotLevel("Change object segment");

        ImGui::TableRowLabel("Type");
        ImGui::SetNextItemWidth(-1);

        if (obj.Type == ObjectType::SecretExitReturn) {
            ImGui::Text("Secret Exit Return");
        }
        else if (ImGui::BeginCombo("##Type", GetObjectTypeName(obj.Type))) {
            static constexpr std::array AVAILABLE_TYPES = {
                ObjectType::Robot,
                ObjectType::Powerup,
                ObjectType::Hostage,
                ObjectType::Player,
                ObjectType::Coop,
                ObjectType::Reactor,
                ObjectType::Weapon
            };

            auto typeCount = Game::Level.IsDescent1() ? AVAILABLE_TYPES.size() - 1 : AVAILABLE_TYPES.size();
            for (int i = 0; i < typeCount; i++) {
                auto type = AVAILABLE_TYPES[i];
                const bool isSelected = obj.Type == type;
                if (ImGui::Selectable(GetObjectTypeName(type), isSelected)) {
                    int8 objId = type == ObjectType::Weapon ? (int8)WeaponID::LevelMine : 0;
                    InitObject(obj, type, objId);
                    if (type == ObjectType::Weapon) RandomizeMineRotation(obj);

                    ForMarkedObjects([type, objId](Object& o) {
                        if (type == ObjectType::Weapon) RandomizeMineRotation(o);
                        InitObject(o, type, objId);
                    });
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
                    InitObject(obj, obj.Type, obj.ID);
                    ForMarkedObjects([&obj](Object& o) {
                        if (o.Type != obj.Type) return;
                        InitObject(o, obj.Type, obj.ID);
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

        ImGui::TableRowLabel("Type ID");
        ImGui::Text("%i", obj.ID);

        ImGui::TableRowLabel("Light Color");
        ImGui::SetNextItemWidth(-1);
        ImGui::ColorEdit3("##customcolor", &obj.Light.Color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

        ImGui::TableRowLabel("Light Radius");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##RADIUS", &obj.Light.Radius, 0, 50);
    }
}
