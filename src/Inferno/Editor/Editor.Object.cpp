#include "pch.h"
#include "Editor.Object.h"
#include "Editor.h"
#include "Game.h"
#include "Game.Segment.h"
#include "Gizmo.h"
#include "Resources.h"
#include "Graphics/Render.h"

namespace Inferno::Editor {
    void DeleteObject(Level& level, ObjID id) {
        auto pObj = level.TryGetObject(id);
        if (!pObj) return;

        Events::ObjectsChanged();
        Seq::removeAt(level.Objects, (int)id);

        // Shift room objects
        for (auto& seg : level.Segments) {
            for (auto& objId : seg.Objects) {
                if (objId == id) objId = ObjID::None;
                if (objId > id) objId--;
            }
        }
    }

    // If center is true the object is moved to segment center, otherwise it is moved to the selected face.
    // The object is aligned to the selected edge in both cases.
    bool MoveObjectToSide(Level& level, ObjID id, PointTag tag, bool center) {
        auto obj = level.TryGetObject(id);
        auto seg = level.TryGetSegment(tag.Segment);
        if (!obj || !seg) return false;

        auto face = Face::FromSide(level, *seg, tag.Side);
        auto edge = face.VectorForEdge(tag.Point);
        auto normal = face.AverageNormal();
        //auto& normal = face.Side.NormalForEdge(tag.Point);

        Matrix transform;
        transform.Up(normal);
        transform.Forward(edge.Cross(normal));
        transform.Right(edge);

        float distance = obj->Radius;

        if (obj->Render.Type == RenderType::Model) {
            auto& model = Resources::GetModel(obj->Render.Model.ID);
            distance = -model.MinBounds.y;
        }

        if (center)
            transform.Translation(seg->Center);
        else
            transform.Translation(face.Center() + normal * distance); // position on face

        obj->Segment = tag.Segment;
        obj->SetTransform(transform);
        return true;
    }

    bool MoveObjectToSegment(Level& level, ObjID id, SegID segId) {
        auto obj = level.TryGetObject(id);
        auto seg = level.TryGetSegment(segId);
        if (!obj || !seg) return false;

        obj->Segment = segId;
        obj->Position = seg->Center;
        return true;
    }

    bool MoveObject(Level& level, ObjID id, Vector3 position) {
        auto obj = level.TryGetObject(id);
        if (!obj) return false;

        obj->Position = position;

        // Leave the last good ID if nothing contains the object
        auto segId = FindContainingSegment(level, position);
        if (segId != SegID::None) obj->Segment = segId;
        return true;
    }

    // Rotates an object to face towards a side
    void AlignObjectToSide(Level& level, Object& obj, PointTag tag) {
        auto seg = level.TryGetSegment(tag.Segment);
        if (!seg) return;

        auto face = Face::FromSide(level, *seg, tag.Side);
        auto edge = face.VectorForEdge(tag.Point);
        auto& normal = face.Side.NormalForEdge(tag.Point);

        Matrix3x3 transform;
        transform.Up(edge.Cross(-normal));
        transform.Forward(-normal);
        transform.Right(-edge);
        obj.Rotation = transform;
    }

    int GetObjectCount(const Level& level, ObjectType type) {
        int i = 0;
        for (auto& obj : level.Objects)
            if (obj.Type == type) i++;

        return i;
    }

    float GetObjectRadius(const Object& obj) {
        constexpr float playerRadius = FixToFloat(0x46c35L);

        switch (obj.Type) {
            case ObjectType::Player:
            case ObjectType::Coop:
                return playerRadius;

            case ObjectType::Robot:
            {
                auto& ri = Resources::GetRobotInfo(obj.ID);
                if (ri.Radius > 0)
                    return ri.Radius;
                else
                    return Resources::GetModel(ri.Model).Radius;
            }

            case ObjectType::Hostage:
                return 5;

            case ObjectType::Powerup:
                return Resources::GetPowerup((PowerupID)obj.ID).Size;

            case ObjectType::Reactor:
            {
                if (auto info = Seq::tryItem(Resources::GameData.Reactors, obj.ID))
                    return Resources::GetModel(info->Model).Radius;
                else
                    return obj.Radius;
            }

            case ObjectType::Weapon:
            {
                if (obj.Render.Type == RenderType::Model) {
                    return Resources::GetModel(obj.Render.Model.ID).Radius;
                }
                else {
                    return obj.Radius;
                }
            }
        }

        return 5;
    }

    void InitPlaceableMine(Object& obj) {
        auto& weapon = Resources::GetWeapon(WeaponID::LevelMine);

        obj.Control.Type = ControlType::Weapon;
        obj.Control.Weapon.Parent = {};
        obj.Control.Weapon.ParentType = obj.Type;
        obj.Movement = MovementType::Physics;

        obj.Physics.Mass = weapon.Mass;
        obj.Physics.Drag = weapon.Drag;
        obj.Physics.Flags = PhysicsFlag::Bounce | PhysicsFlag::FixedAngVel;
        obj.ID = (int8)WeaponID::LevelMine;
        obj.Render.Type = RenderType::Model;
        obj.Render.Model = { .ID = ModelID::Mine };
        obj.HitPoints = 20;
    }

    void InitObject(const Level& level, Object& obj, ObjectType type, int8 id, bool fullReset) {
        const ModelID coopModel = level.IsDescent1() ? ModelID::D1Coop : ModelID::D2Player;

        obj.Type = type;
        obj.ID = id;
        obj.Movement = {};
        if (fullReset) obj.Control = {};
        obj.Render = {};
        obj.Light = {};
        obj.Physics = {};

        switch (type) {
            case ObjectType::Player:
            {
                obj.Control.Type = obj.ID == 0 ? ControlType::None : ControlType::Slew; // Player 0 only
                obj.Movement = MovementType::Physics;

                const auto& ship = Resources::GameData.PlayerShip;
                auto& physics = obj.Physics;
                physics.Brakes = physics.TurnRoll = 0;
                physics.Drag = ship.Drag;
                physics.Mass = ship.Mass;

                physics.Flags |= PhysicsFlag::TurnRoll | PhysicsFlag::AutoLevel | PhysicsFlag::Wiggle | PhysicsFlag::UseThrust;
                obj.Render.Type = RenderType::Model;
                obj.Render.Model = {
                    .ID = ship.Model,
                    .TextureOverride = LevelTexID::None
                };

                for (auto& angle : obj.Render.Model.Angles)
                    angle = Vector3::Zero;

                obj.Flags = (ObjectFlag)0;
                obj.ID = 0; // can only have one ID 0 player, fix it later
                break;
            }

            case ObjectType::Coop:
                obj.Movement = MovementType::Physics;
                obj.Render.Type = RenderType::Model;
                obj.Render.Model = { .ID = coopModel };
                obj.ID = 0;
                break;

            case ObjectType::Robot:
            {
                auto& ri = Resources::GetRobotInfo(id);
                obj.Control.Type = ControlType::AI;
                obj.Movement = MovementType::Physics;
                obj.Physics.Mass = ri.Mass;
                obj.Physics.Drag = ri.Drag;
                obj.Render.Type = RenderType::Model;
                obj.HitPoints = ri.HitPoints;
                obj.Render.Model = { .ID = ri.Model };
                if (ri.Cloaking != CloakType::None) obj.Cloak((float)obj.Lifespan);

                if (fullReset) {
                    obj.Control.AI.Behavior = AIBehavior::Normal;
                    obj.Contains.Type = ObjectType::None;
                }
                break;
            }
            case ObjectType::Hostage:
                obj.Control.Type = ControlType::Powerup;
                obj.Render.Type = RenderType::Hostage;
                obj.Render.VClip = { .ID = VClipID(33) };
                break;

            case ObjectType::Powerup:
            {
                obj.Control.Type = ControlType::Powerup;
                obj.Render.Type = RenderType::Powerup;
                auto& info = Resources::GetPowerup((PowerupID)id);
                obj.Render.VClip = { .ID = info.VClip };
                obj.Radius = info.Size;
                obj.Light.Radius = info.LightRadius;
                obj.Light.Color = info.LightColor;
                obj.Light.Mode = info.LightMode;
                obj.Render.Emissive = info.Glow;

                if ((PowerupID)id == PowerupID::Vulcan || (PowerupID)id == PowerupID::Gauss)
                    obj.Control.Powerup.Count = VULCAN_AMMO_PICKUP;

                break;
            }

            case ObjectType::Reactor:
            {
                obj.Control.Type = ControlType::Reactor;
                obj.Render.Type = RenderType::Model;

                if (Seq::inRange(Resources::GameData.Reactors, id)) {
                    auto& info = Resources::GameData.Reactors[id];
                    obj.Render.Model = { .ID = info.Model };
                }

                obj.HitPoints = 200;
                break;
            }

            case ObjectType::Weapon: // For placeable mines
            {
                obj.Physics.Flags = {};
                auto& weapon = Resources::GetWeapon((WeaponID)id);
                if (weapon.Extended.PointCollideWalls)
                    obj.Physics.Flags = PhysicsFlag::PointCollideWalls;

                if ((WeaponID)id == WeaponID::LevelMine)
                    InitPlaceableMine(obj);

                break;
            }
        }

        obj.Radius = GetObjectRadius(obj);
        obj.MaxHitPoints = obj.HitPoints;
        obj.NextThinkTime = 0;

        if (obj.Render.Type == RenderType::Model)
            Render::LoadModelDynamic(obj.Render.Model.ID);

        if (obj.Render.Type == RenderType::Hostage || obj.Render.Type == RenderType::Powerup)
            Render::LoadTextureDynamic(obj.Render.VClip.ID);
    }

    ObjID AddObject(Level& level, PointTag tag, Object obj) {
        if (!level.SegmentExists(tag)) return ObjID::None;

        if (level.Objects.size() + 1 >= level.Limits.Objects) {
            ShowWarningMessage(L"Out of room for objects!");
            return ObjID::None;
        }

        switch (obj.Type) {
            case ObjectType::Player:
                if (GetObjectCount(level, ObjectType::Player) >= level.Limits.Players) {
                    SetStatusMessageWarn("Cannot add more than {} players!", level.Limits.Players);
                    InitObject(level, obj, ObjectType::Powerup);
                }
                break;

            case ObjectType::Coop:
                if (GetObjectCount(level, ObjectType::Coop) >= level.Limits.Coop) {
                    SetStatusMessageWarn("Cannot add more than {} co-op players!", level.Limits.Coop);
                    InitObject(level, obj, ObjectType::Powerup);
                }
                break;

            case ObjectType::Reactor:
                // Disable reactor check as some builds allow multiples
                //if (GetObjectCount(level, ObjectType::Reactor) >= level.Limits.Reactor) {
                //    SetStatusMessage("Cannot add more than {} reactor!", level.Limits.Reactor);
                //    return ObjID::None;
                //}
                break;
        }

        auto id = (ObjID)level.Objects.size();
        level.Objects.push_back(obj);

        Selection.SetSelection(id);
        MoveObjectToSide(level, id, tag, true);
        Editor::Gizmo.UpdatePosition();

        Events::TexturesChanged();
        Events::ObjectsChanged();
        return id;
    }

    ObjID AddObject(Level& level, PointTag tag, ObjectType type) {
        Object obj{};
        InitObject(level, obj, type);
        return AddObject(level, tag, obj);
    }

    // Adds an object to represent the secret exit return so it can be manipulated
    void AddSecretLevelReturnMarker(Level& level) {
        Object marker{};
        marker.Type = ObjectType::SecretExitReturn;
        marker.Render.Type = RenderType::Model;
        //marker.Render.Model.ID = Resources::GameData.MarkerModel;
        marker.Render.Model.ID = Resources::GameData.PlayerShip.Model;
        marker.Render.Model.TextureOverride = LevelTexID(426);
        marker.Radius = 5;

        if (!level.SegmentExists(level.SecretExitReturn))
            level.SecretExitReturn = {};

        marker.Segment = level.SecretExitReturn;
        marker.Rotation = level.SecretReturnOrientation;
        if (auto seg = level.TryGetSegment(level.SecretExitReturn))
            marker.Position = seg->Center;

        level.Objects.push_back(marker);
        Render::LoadModelDynamic(marker.Render.Model.ID);
    }

    void RemoveSecretLevelReturnMarker(Level& level) {
        int i = 0;
        for (; i < level.Objects.size(); i++) {
            if (level.Objects[i].Type == ObjectType::SecretExitReturn) break;
        }

        DeleteObject(level, ObjID(i));
    }

    void UpdateSecretLevelReturnMarker() {
        auto& level = Game::Level;
        if (!level.IsDescent2()) return;

        if (level.HasSecretExit())
            AddSecretLevelReturnMarker(level);
        else
            RemoveSecretLevelReturnMarker(level);
    }

    namespace Commands {
        Command AlignObjectToSide{
            .SnapshotAction = [] {
                auto obj = Game::Level.TryGetObject(Editor::Selection.Object);
                if (!obj) return "";

                Editor::AlignObjectToSide(Game::Level, *obj, Editor::Selection.PointTag());
                Editor::Gizmo.UpdatePosition();
                return "Align Object To Side";
            },
            .Name = "Align Object To Side"
        };

        Command MoveObjectToSide{
            .SnapshotAction = [] {
                if (!Editor::MoveObjectToSide(Game::Level, Editor::Selection.Object, Editor::Selection.PointTag(), false))
                    return "";

                Editor::Gizmo.UpdatePosition();
                return "Move Object to Side";
            },
            .Name = "Move Object to Side"
        };

        Command MoveObjectToSegment{
            .SnapshotAction = [] {
                if (!Editor::MoveObjectToSegment(Game::Level, Editor::Selection.Object, Editor::Selection.Segment))
                    return "";

                Editor::Gizmo.UpdatePosition();
                return "Move Object to Segment";
            },
            .Name = "Move Object to Segment"
        };

        Command MoveObjectToUserCSys{
            .SnapshotAction = [] {
                if (!Editor::MoveObject(Game::Level, Editor::Selection.Object, Editor::UserCSys.Translation()))
                    return "";

                Editor::Gizmo.UpdatePosition();
                return "Move Object to User Coordinate System";
            },
            .Name = "Move Object to UCS"
        };

        Command AddObject{
            .SnapshotAction = [] {
                if (auto obj = Game::Level.TryGetObject(Editor::Selection.Object)) {
                    Object copy = *obj;
                    auto id = Editor::AddObject(Game::Level, Selection.PointTag(), copy);
                    if (id == ObjID::None) return "";
                    Editor::Selection.Object = id;
                    return "Add Object";
                }
                else {
                    auto type = Game::Level.Objects.empty() ? ObjectType::Player : ObjectType::Robot;
                    auto id = Editor::AddObject(Game::Level, Selection.PointTag(), type);
                    if (id == ObjID::None) return "";
                    return "Add Object";
                }
            },
            .Name = "Add Object"
        };
    }
}
