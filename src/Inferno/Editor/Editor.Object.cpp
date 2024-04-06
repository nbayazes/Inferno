#include "pch.h"
#include "Editor.Object.h"
#include "Editor.h"
#include "Game.h"
#include "Game.Object.h"
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

        // Recalculate normal in case side isn't flat
        auto forward = edge.Cross(normal);
        forward.Normalize();
        auto right = -forward.Cross(normal);
        auto up = forward.Cross(right);

        Matrix transform;
        transform.Forward(forward);
        transform.Right(right);
        transform.Up(up);

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
                    InitObject(obj, ObjectType::Powerup);
                }
                break;

            case ObjectType::Coop:
                if (GetObjectCount(level, ObjectType::Coop) >= level.Limits.Coop) {
                    SetStatusMessageWarn("Cannot add more than {} co-op players!", level.Limits.Coop);
                    InitObject(obj, ObjectType::Powerup);
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
        InitObject(obj, type);
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
