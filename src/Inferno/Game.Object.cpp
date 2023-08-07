#include <pch.h>

#include "Level.h"
#include "Game.Object.h"
#include "Game.h"
#include "Game.Segment.h"
#include "Game.Wall.h"

namespace Inferno {
    uint8 GetGunSubmodel(const Object& obj, uint8 gun) {
        if (obj.IsRobot()) {
            auto& robot = Resources::GetRobotInfo(obj.ID);
            return robot.GunSubmodels[gun];
        }

        return 0;
    }

    Tuple<Vector3, Vector3> GetSubmodelOffsetAndRotation(const Object& object, const Model& model, int submodel) {
        if (!Seq::inRange(model.Submodels, submodel)) return { Vector3::Zero, Vector3::Zero };

        // accumulate the offsets for each submodel
        auto submodelOffset = Vector3::Zero;
        Vector3 submodelAngle = object.Render.Model.Angles[submodel];
        auto* smc = &model.Submodels[submodel];
        while (smc->Parent != ROOT_SUBMODEL) {
            auto& parentAngle = object.Render.Model.Angles[smc->Parent];
            auto parentRotation = Matrix::CreateFromYawPitchRoll(parentAngle);
            submodelOffset += Vector3::Transform(smc->Offset, parentRotation);
            submodelAngle += object.Render.Model.Angles[smc->Parent];
            smc = &model.Submodels[smc->Parent];
        }

        return { submodelOffset, submodelAngle };
    }

    Vector3 GetSubmodelOffset(const Object& obj, SubmodelRef submodel) {
        if (submodel.ID == -1) 
            return Vector3::Zero;

        auto& model = Resources::GetModel(obj.Render.Model.ID);

        auto sm = submodel.ID;
        while (sm != ROOT_SUBMODEL) {
            auto rotation = Matrix::CreateFromYawPitchRoll(obj.Render.Model.Angles[sm]);
            submodel.Offset = Vector3::Transform(submodel.Offset, rotation) + model.Submodels[sm].Offset;
            sm = model.Submodels[sm].Parent;
        }

        return submodel.Offset * Vector3(1, 1, -1);
    }

    SubmodelRef GetLocalGunpointOffset(const Object& obj, uint8 gun) {
        gun = std::clamp(gun, (uint8)0, MAX_GUNS);

        if (obj.Type == ObjectType::Robot) {
            auto& robot = Resources::GetRobotInfo(obj.ID);
            auto gunpoint = robot.GunPoints[gun];
            return { robot.GunSubmodels[gun], gunpoint };
        }

        if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Coop) {
            auto& gunpoint = Resources::GameData.PlayerShip.GunPoints[gun];
            return { 0, gunpoint };
        }

        if (obj.Type == ObjectType::Reactor) {
            if (!Seq::inRange(Resources::GameData.Reactors, obj.ID)) return { 0, Vector3::Zero };
            auto& reactor = Resources::GameData.Reactors[obj.ID];
            return { 0, reactor.GunPoints[gun] };
        }

        return { 0, Vector3::Zero };
    }

    Vector3 GetGunpointOffset(const Object& obj, uint8 gun) {
        gun = std::clamp(gun, (uint8)0, MAX_GUNS);

        if (obj.Type == ObjectType::Robot) {
            auto& robot = Resources::GetRobotInfo(obj.ID);
            auto& model = Resources::GetModel(robot.Model);
            auto gunpoint = robot.GunPoints[gun];
            auto submodel = robot.GunSubmodels[gun];

            while (submodel != ROOT_SUBMODEL) {
                auto rotation = Matrix::CreateFromYawPitchRoll(obj.Render.Model.Angles[submodel]);
                gunpoint = Vector3::Transform(gunpoint, rotation) + model.Submodels[submodel].Offset;
                submodel = model.Submodels[submodel].Parent;
            }

            return gunpoint * Vector3(1, 1, -1);
        }

        if (obj.Type == ObjectType::Player || obj.Type == ObjectType::Coop) {
            return Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1);
            //offset = Resources::GameData.PlayerShip.GunPoints[gun] * Vector3(1, 1, -1);
        }

        if (obj.Type == ObjectType::Reactor) {
            if (!Seq::inRange(Resources::GameData.Reactors, obj.ID)) return Vector3::Zero;
            auto& reactor = Resources::GameData.Reactors[obj.ID];
            return reactor.GunPoints[gun];
        }

        return Vector3::Zero;
    }

    bool UpdateObjectSegment(Level& level, Object& obj) {
        if (PointInSegment(level, obj.Segment, obj.Position))
            return false; // Already in the right segment

        auto id = FindContainingSegment(level, obj.Position);
        // Leave the last good ID if nothing contains the object
        if (id != SegID::None) obj.Segment = id;

        auto& seg = level.GetSegment(obj.Segment);
        auto transitionTime = Game::GetState() == GameState::Game ? 0.5f : 0;
        obj.Ambient.SetTarget(seg.VolumeLight, Game::Time, transitionTime);
        return true;
    }

    void MoveObject(Level& level, ObjID objId) {
        auto pObj = level.TryGetObject(objId);
        if (!pObj) return;
        auto& obj = *pObj;
        auto prevSegId = obj.Segment;

        if (!UpdateObjectSegment(level, obj))
            return; // already in the right segment

        if (obj.Segment == SegID::None)
            return; // Object was outside of world

        Tag connection{};

        // Check if the new position is in a touching segment, because fast moving objects can cross
        // multiple segments in one update. This affects gauss the most.
        auto& prevSeg = level.GetSegment(prevSegId);
        for (auto& side : SideIDs) {
            auto cid = prevSeg.GetConnection(side);
            if (PointInSegment(level, cid, obj.Position)) {
                connection = { prevSegId, side };
                break;
            }
        }

        if (connection && obj.Type == ObjectType::Player) {
            // Activate triggers
            if (auto trigger = level.TryGetTrigger(connection)) {
                fmt::print("Activating fly through trigger {}:{}\n", connection.Segment, connection.Side);
                ActivateTrigger(level, *trigger);
            }
        }
        else if (!connection) {
            // object crossed multiple segments in a single update.
            // usually caused by fast moving projectiles, but can also happen if object is outside world.
            if (obj.Type == ObjectType::Player && prevSegId != obj.Segment)
                SPDLOG_WARN("Player {} warped from segment {} to {}. Any fly-through triggers did not activate!", objId, prevSegId, obj.Segment);
        }

        // Update object pointers
        prevSeg.RemoveObject(objId);
        auto& seg = level.GetSegment(obj.Segment);
        seg.AddObject(objId);
        obj.Room = level.FindRoomBySegment(obj.Segment); // todo: track using portals
        obj.Ambient.SetTarget(seg.VolumeLight, Game::Time, 0.25f);
    }

    const std::set BOSS_IDS = { 17, 23, 31, 45, 46, 52, 62, 64, 75, 76 };

    bool IsBossRobot(const Object& obj) {
        return obj.Type == ObjectType::Robot && BOSS_IDS.contains(obj.ID);
    }
}
