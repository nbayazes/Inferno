#include "pch.h"
#include "logging.h"
#include "Editor.Diagnostics.h"
#include "Editor.Wall.h"
#include "Editor.Object.h"
#include "Editor.Geometry.h"
#include "Face.h"

namespace Inferno::Editor {
    // The three adjacent points of a segment for each corner
    constexpr ubyte AdjacentPointTable[8][3] = {
        { 1, 3, 4 },
        { 2, 0, 5 },
        { 3, 1, 6 },
        { 0, 2, 7 },
        { 7, 5, 0 },
        { 4, 6, 1 },
        { 5, 7, 2 },
        { 6, 4, 3 }
    };

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

    float CalcAngle(const Level& level, short i0, short i1, short i2, short i3) {
        auto& v0 = level.Vertices[i0];
        auto& v1 = level.Vertices[i1];
        auto& v2 = level.Vertices[i2];
        auto& v3 = level.Vertices[i3];

        const auto line1 = v1 - v0;
        const auto line2 = v2 - v0;
        const auto line3 = v3 - v0;

        // use cross product to calcluate orthogonal vector
        auto ortho = -line1.Cross(line2);

        // use dot product to determine angle A dot B = |A|*|B| * cos (angle)
        // therfore: angle = acos (A dot B / |A|*|B|)
        auto dot = line3.Dot(ortho);
        auto mag1 = line3.Length();
        auto mag2 = ortho.Length();

        if (dot == 0 || mag1 == 0 || mag2 == 0) {
            return 200 * DegToRad;
        }
        else {
            auto ratio = dot / (mag1 * mag2);
            if (ratio < -1.0f || ratio > 1.0f)
                return 199 * DegToRad;
            else
                return acos(ratio);
        }
    }

    // Calculates an angle between three vectors sharing v0
    float AngleBetweenThreeVectors(const Vector3& v0, const Vector3& v1, const Vector3& v2, const Vector3& v3) {
        auto line1 = v1 - v0;
        auto line2 = v2 - v0;
        auto line3 = v3 - v0;
        // use cross product to calcluate orthogonal vector
        auto orthog = -line1.Cross(line2);

        // use dot product to determine angle A dot B = |A|*|B| * cos (angle)
        // therfore: angle = acos (A dot B / |A|*|B|)
        auto dot = line3.Dot(orthog);
        auto magnitude1 = line3.Length();
        auto magnitude2 = orthog.Length();

        if (dot == 0 || magnitude1 == 0 || magnitude2 == 0) {
            return 200 * DegToRad;
        }
        else {
            auto ratio = dot / (magnitude1 * magnitude2);
            ratio = float((int)(ratio * 1000.0f)) / 1000.0f; // round

            if (ratio < -1.0f || ratio > 1.0f)
                return 199 * DegToRad;
            else
                return acos(ratio);
        }
    }

    // returns true if segment is degenerate
    bool SegmentIsDegenerate(Level& level, Segment& seg) {
        for (short nPoint = 0; nPoint < 8; nPoint++) {
            // define vert numbers
            const auto& vert0 = level.Vertices[seg.Indices[nPoint]];
            const auto& vert1 = level.Vertices[seg.Indices[AdjacentPointTable[nPoint][0]]];
            const auto& vert2 = level.Vertices[seg.Indices[AdjacentPointTable[nPoint][1]]];
            const auto& vert3 = level.Vertices[seg.Indices[AdjacentPointTable[nPoint][2]]];

            if (AngleBetweenThreeVectors(vert0, vert1, vert2, vert3) > DirectX::XM_PIDIV2)
                return true;
            if (AngleBetweenThreeVectors(vert0, vert2, vert3, vert1) > DirectX::XM_PIDIV2)
                return true;
            if (AngleBetweenThreeVectors(vert0, vert3, vert1, vert2) > DirectX::XM_PIDIV2)
                return true;
        }

        return false;
    }

    // Returns false is flatness is invalid
    bool CheckSegmentFlatness(Level& level, Segment& seg) {
        // also test for flatness
        for (int nSide = 0; nSide < 6; nSide++) {
            auto face = Face::FromSide(level, seg, (SideID)nSide);
            auto flatness = face.FlatnessRatio();
            if (flatness <= 0.80f)
                return false;
        }

        return true;
    }

    bool SidesMatch(Level& level, Tag srcTag, Tag destTag) {
        auto& src = level.GetSegment(srcTag);
        auto& dest = level.GetSegment(destTag);

        short match[4]{};
        for (int i = 0; i < 4; i++) {
            auto u = src.GetVertexIndices(srcTag.Side)[i];
            for (int j = 0; j < 4; j++) {
                auto v = dest.GetVertexIndices(destTag.Side)[j];
                if (u == v)
                    match[i]++;
            }
        }

        for (auto& m : match) // Check that each point is matched exactly once
            if (m != 1) return false;

        return true;
    }

    List<SegmentDiagnostic> CheckSegments(Level& level) {
        List<SegmentDiagnostic> results;

        for (int i = 0; i < level.Segments.size(); i++) {
            auto& seg = level.Segments[i];
            auto segid = SegID(i);

            if (seg.Type == SegmentType::Matcen) {
                // this doesn't check links, but matcens need to be sorted for that
                if (!level.TryGetMatcen(seg.Matcen)) {
                    results.push_back({ 0, { segid, SideID::None }, "Matcen data is missing" });
                }
            }

            if (SegmentIsDegenerate(level, seg)) {
                results.push_back({ 0, { segid, SideID::None }, "Degenerate geometry" });
                continue; // Geometry is too deformed to bother reporting the other checks
            }

            if (!CheckSegmentFlatness(level, seg)) {
                results.push_back({ 0, { segid, SideID::None }, "Geometry flatness" });
            }

            for (auto& side : SideIDs) {
                auto connId = seg.GetConnection(side);
                if (connId == SegID::Exit || connId == SegID::None) continue;

                if (!level.SegmentExists(connId)) {
                    auto msg = fmt::format("Illegal segment connection {}", connId);
                    results.push_back({ 0, { segid, side }, msg });
                    // break conn?
                }

                if (auto other = level.GetConnectedSide({ segid, side })) {
                    // Check that vertices match between connections
                    if (!SidesMatch(level, { segid, side }, other)) {
                        results.push_back({ 1, { segid, side }, "Connection vertex mismatch" });
                    }
                }
                else {
                    auto msg = fmt::format("Segment does not connect to {}", connId);
                    results.push_back({ 0, { segid, side }, msg });
                }
            }

        }

        return results;
    }

    bool HasExitConnection(const Level& level) {
        for (auto& seg : level.Segments) {
            for (auto& c : seg.Connections) {
                if (c == SegID::Exit) return true;
            }
        }

        return false;
    }

    void CheckLevelForErrors(const Level& level) {
        wstring warnings;

        if (GetObjectCount(level, ObjectType::Player) == 0) {
            warnings += L"Level does not contain a player start.\n\n";
        }

        if (GetObjectCount(level, ObjectType::Reactor) > 1) {
            warnings +=
                L"Level contains more than one reactor. "
                L"This will result in odd behavior in old versions.\n\n";
        }

        auto boss = Seq::findIndex(Game::Level.Objects, IsBossRobot);
        auto reactor = Seq::findIndex(Game::Level.Objects, IsReactor);

        if ((boss || reactor) && !HasExitConnection(level)) {
            warnings +=
                L"Level has a boss or reactor but no end of exit tunnel is marked. "
                L"This will crash some versions of Descent at end of level.\n\n";
        }

        constexpr auto title = L"Level Error Check";

        if (warnings.empty())
            ShowOkMessage(L"No errors found", title);
        else
            ShowWarningMessage(warnings, title);
    }
}
