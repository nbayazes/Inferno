#include "pch.h"
#include "logging.h"
#include "Editor.Diagnostics.h"
#include "Editor.Wall.h"
#include "Editor.Object.h"
#include "Editor.Geometry.h"

namespace Inferno::Editor {
    void FixObjects(Level& level) {
        bool hasPlayerStart = GetObjectCount(level, ObjectType::Player) > 0;

        if (hasPlayerStart) {
            if (level.Objects[0].Type != ObjectType::Player) {
                SPDLOG_WARN("Level contains a player start but it was not the first object. Swapping objects.");
                auto index = Seq::findIndex(level.Objects, [](Object& obj) { return obj.Type == ObjectType::Player; });
                std::swap(level.Objects[0], level.Objects[*index]);
                Events::SelectObject();
            }
        }

        for (int id = 0; id < level.Objects.size(); id++) {
            auto& obj = level.GetObject((ObjID)id);
            if (obj.Type == ObjectType::Weapon) {
                obj.Control.Weapon.Parent = (ObjID)id;
                obj.Control.Weapon.ParentSig = (ObjSig)id;
                obj.Control.Weapon.ParentType = obj.Type;
            }

            NormalizeObjectVectors(obj);
        }
    }

    void FixWalls(Level& level) {
        // Relink walls
        for (int segid = 0; segid < level.Segments.size(); segid++) {
            for (auto& sid : SideIDs) {
                Tag tag((SegID)segid, sid);
                auto& side = level.GetSide(tag);
                if (side.Wall == WallID::None) continue;

                if (auto wall = level.TryGetWall(side.Wall)) {
                    if (wall->Tag != tag) {
                        SPDLOG_WARN("Fixing mismatched wall tag on segment {}:{}", (int)tag.Segment, (int)tag.Side);
                        wall->Tag = tag;
                    }
                }
                else {
                    SPDLOG_WARN("Removing wall {} from {}:{} because it doesn't exist", (int)side.Wall, (int)tag.Segment, (int)tag.Side);
                    side.Wall = WallID::None;
                }
            }
        }

        // Fix VClip 2
        for (int id = 0; id < level.Walls.size(); id++) {
            auto& wall = level.GetWall((WallID)id);
            wall.LinkedWall = WallID::None; // Wall links are only valid during runtime
            if (wall.Clip == WClipID(2)) { // ID 2 is bad and has no animation
                if (FixWallClip(wall))
                    SPDLOG_WARN("Fixed invalid wall clip on {}:{}", wall.Tag.Segment, wall.Tag.Side);
            }
        }
    }

    void FixTriggers(Level& level) {
        for (int id = 0; id < level.Triggers.size(); id++) {
            auto& trigger = level.GetTrigger((TriggerID)id);

            for (int t = (int)trigger.Targets.Count() - 1; t > 0; t--) {
                auto& tag = trigger.Targets[t];
                if (!level.SegmentExists(tag)) {
                    SPDLOG_WARN("Removing invalid trigger target. TID: {} - {}:{}", id, tag.Segment, tag.Side);
                    tag = {};
                    trigger.Targets.Remove(t);
                }
            }
        }

        for (int t = (int)level.ReactorTriggers.Count() - 1; t > 0; t--) {
            auto& tag = level.ReactorTriggers[t];
            if (!level.SegmentExists(tag)) {
                SPDLOG_WARN("Removing invalid reactor trigger target. {}:{}", tag.Segment, tag.Side);
                tag = {};
                level.ReactorTriggers.Remove(t);
            }
        }
    }

    void FixMatcens(Level& level) {
        List<Matcen> matcens = level.Matcens;

        // Matcens must be sorted ascending order
        Seq::sortBy(matcens, [](Matcen& a, Matcen& b) { return a.Segment < b.Segment; });

        level.Matcens.clear();

        for (int i = 0; i < matcens.size(); i++) {
            if (auto seg = level.TryGetSegment(matcens[i].Segment)) {
                seg->Matcen = MatcenID(i);
                level.Matcens.push_back(matcens[i]);
            }
            else {
                SPDLOG_WARN("Removing orphan matcen id {}", i);
            }
        }
    }

    void SetPlayerStartIDs(Level& level) {
        int8 id = 0;
        for (auto& i : level.Objects) {
            if (i.Type == ObjectType::Player)
                i.ID = id++;
        }

        id = 8; // it's unclear if setting co-op IDs is necessary, but do it anyway.
        for (auto& i : level.Objects) {
            if (i.Type == ObjectType::Coop)
                i.ID = id++;
        }
    }

    void FixSegmentConnections(Level& level) {
        for (int srcid = 0; srcid < level.Segments.size(); srcid++) {
            auto& src = level.Segments[srcid];

            for (auto& srcSide : SideIDs) {
                auto dstId = src.GetConnection(srcSide);
                if ((int)dstId < 0) continue;

                auto dstSide = level.GetConnectedSide((SegID)srcid, dstId);

                if (dstSide == SideID::None) {
                    src.Connections[(int)srcSide] = SegID::None;
                    SPDLOG_WARN("Removed one sided connection at segment {}:{}", srcid, srcSide);
                    continue;
                }

                auto& dst = level.GetSegment(dstId);
                auto srcVerts = src.GetVertexIndices(srcSide);
                auto dstVerts = dst.GetVertexIndices(dstSide);

                // Check that the indices match
                bool mismatched = false;
                for (auto& sv : srcVerts) {
                    if (!Seq::contains(dstVerts, sv)) {
                        mismatched = true;
                        break;
                    }
                }

                if (mismatched) {
                    src.Connections[(int)srcSide] = SegID::None;
                    dst.Connections[(int)dstSide] = SegID::None;
                    SPDLOG_WARN("Removed invalid connections at segment {}:{} and {}:{}", srcid, srcSide, dstId, dstSide);
                    continue;
                }

                //auto srcType = src.GetSide(srcSide).Type;
                //auto dstType = dst.GetSide(dstSide).Type;

                // The following is an incomplete attempt at fixing mismatched triangulation. Probably not necessary.
                //if (srcType == SideSplitType::Quad && dstType == SideSplitType::Quad) {
                //    int t = 0;
                //    // find where verts match
                //    for (; t < 4 && srcVerts[t] != dstVerts[0]; t++);

                //    if (t == 4 ||
                //        srcVerts[0] != dstVerts[t] ||
                //        srcVerts[1] != dstVerts[(t + 3) % 4] ||
                //        srcVerts[2] != dstVerts[(t + 2) % 4] ||
                //        srcVerts[3] != dstVerts[(t + 1) % 4]) {
                //        SPDLOG_WARN("Vertex mismatch between sides {}:{} and {}:{}", srcid, srcSide, dstId, dstSide);
                //    }
                //    else {
                //        // normal checks
                //    }
                //}
                //else if (srcType != SideSplitType::Quad && dstType != SideSplitType::Quad) {
                //    if (srcVerts[1] == dstVerts[1]) {
                //        if (srcVerts[4] != dstVerts[4] ||
                //            srcVerts[0] != dstVerts[2] ||
                //            srcVerts[2] != dstVerts[0] ||
                //            srcVerts[3] != dstVerts[5] ||
                //            srcVerts[5] != dstVerts[3]) {
                //            auto type = SideSplitType(5 - (int)dst.GetSide(dstSide).Type);
                //            SPDLOG_WARN("Changing seg {}:{} triangulation to {}", srcid, srcSide, type);
                //            //src.GetSide(srcSide).Type = type;
                //        }
                //        else {
                //            // normal checks
                //        }
                //    }
                //    else {
                //        if (srcVerts[1] != dstVerts[4] ||
                //            srcVerts[4] != dstVerts[1] ||
                //            srcVerts[0] != dstVerts[5] ||
                //            srcVerts[5] != dstVerts[0] ||
                //            srcVerts[2] != dstVerts[3] ||
                //            srcVerts[3] != dstVerts[2]) {
                //            auto type = SideSplitType(5 - (int)dst.GetSide(dstSide).Type);
                //            SPDLOG_WARN("Changing seg {}:{} triangulation to {}", srcid, srcSide, type);
                //            //src.GetSide(srcSide).Type = type;
                //        }
                //        else {

                //        }
                //    }
                //}
                //else {
                //    SPDLOG_WARN("Side type mismatch between {}:{} and {}:{}", srcid, srcSide, dstId, dstSide);
                //}
            }
        }

        Events::LevelChanged();
    }
    void FixLevel(Level& level) {
        WeldVertices(level, 0.01f);
        FixSegmentConnections(level);
        FixObjects(level);
        FixWalls(level);
        FixTriggers(level);
        SetPlayerStartIDs(level);
        FixMatcens(level);

        if (!level.SegmentExists(level.SecretExitReturn))
            level.SecretExitReturn = SegID(0);
    }
}