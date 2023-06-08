#include <pch.h>

#include "Level.h"
#include "Game.Object.h"
#include "Game.h"
#include "Game.Segment.h"
#include "Game.Wall.h"

namespace Inferno {
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
        obj.Ambient.SetTarget(seg.VolumeLight, Game::Time, 0.25f);
    }

    const std::set BOSS_IDS = { 17, 23, 31, 45, 46, 52, 62, 64, 75, 76 };

    bool IsBossRobot(const Object& obj) {
        return obj.Type == ObjectType::Robot && BOSS_IDS.contains(obj.ID);
    }
}
