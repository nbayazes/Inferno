#include "pch.h"
#include "Types.h"
#include "Level.h"
#include "Editor.h"
#include "Editor.Segment.h"
#include "Editor.Object.h"
#include "Editor.Wall.h"
#include "Editor.Texture.h"
#include "Graphics/Render.h"

namespace Inferno::Editor {
    void RemoveMatcen(Level& level, MatcenID id) {
        if (id == MatcenID::None) return;

        for (auto& seg : level.Segments)
            if (seg.Matcen > id && seg.Matcen != MatcenID::None) seg.Matcen--;

        if ((int)id < level.Matcens.size())
            Seq::removeAt(level.Matcens, (int)id);
    }


    bool AddMatcen(Level& level, Tag tag) {
        if (!level.CanAddMatcen()) return false;

        if (auto seg = level.TryGetSegment(tag)) {
            seg->Type = SegmentType::Matcen;
            seg->Matcen = (MatcenID)level.Matcens.size();
            auto& m = level.Matcens.emplace_back();
            m.Segment = tag.Segment;
        }

        return true;
    }

    bool SetSegmentType(Level& level, Tag tag, SegmentType type) {
        if (!level.SegmentExists(tag)) return false;
        auto& seg = level.GetSegment(tag);
        if (seg.Type == type) return false; // don't change segs already of this type

        if (seg.Type == SegmentType::Matcen)
            RemoveMatcen(level, seg.Matcen);

        if (type == SegmentType::Reactor) {
            // Add a reactor if one doesn't exist
            if (!Seq::findIndex(level.Objects, IsReactor))
                Editor::AddObject(level, { tag, 0 }, ObjectType::Reactor);
        }
        else if (type == SegmentType::Matcen) {
            if (!Editor::AddMatcen(level, tag))
                return false;
        }

        seg.Type = type;
        return true;
    }

    // Shifts any segment references greater or equal to ref by value.
    // For use with delete / undo
    void ShiftSegmentRefs(Level& level, SegID ref, int value) {
        auto Shift = [value, ref](SegID& id) {
            if (id >= ref) id += (SegID)value;
        };

        // Update connections
        for (auto& seg : level.Segments) {
            for (auto& c : seg.Connections)
                Shift(c);
        }

        // Update object owners
        for (auto& obj : level.Objects)
            Shift(obj.Segment);

        for (auto& matcen : level.Matcens)
            Shift(matcen.Segment);

        // Update triggers
        for (auto& trigger : level.Triggers) {
            for (auto& target : trigger.Targets)
                Shift(target.Segment);
        }

        for (auto& trigger : level.ReactorTriggers)
            Shift(trigger.Segment);

        // Update walls
        for (auto& wall : level.Walls)
            Shift(wall.Tag.Segment);
    }

    // Creates a 20x20 face aligned to the selected edge and centered to the source face
    void CreateOrthoSegmentFace(Level& level, Tag src, int point, Array<uint16, 4>& srcIndices, const Vector3& offset) {
        // Project the existing points
        Vector3 points[4] = {
            level.Vertices[srcIndices[0]] - offset,
            level.Vertices[srcIndices[1]] - offset,
            level.Vertices[srcIndices[2]] - offset,
            level.Vertices[srcIndices[3]] - offset
        };

        Vector3 normal;
        offset.Normalize(normal);

        // move the edge verts towards the center and adjust to size 20
        {
            auto& e0 = points[(point + 0) % 4];
            auto& e1 = points[(point + 1) % 4];

            auto edge = e1 - e0;
            auto edgeAdjust = (edge.Length() - 20) / 2;
            edge.Normalize();
            e0 += edge * edgeAdjust;
            e1 -= edge * edgeAdjust;

            // Position the other two points parallel to edge
            auto up = normal.Cross(edge);
            points[(point + 2) % 4] = e1 + up * offset.Length();
            points[(point + 3) % 4] = e0 + up * offset.Length();
        }

        // center the verts on the source face
        {
            auto center = AverageVectors(points);
            auto face = Face::FromSide(level, src.Segment, src.Side);
            auto projectedCenter = face.Center() - offset;
            auto dist = projectedCenter - center;
            for (auto& p : points) p += dist;
        }

        for (auto& p : points) level.Vertices.push_back(p);
    }

    // Removes any walls or connections on this side and other side
    void BreakConnection(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return;
        auto& seg = level.GetSegment(tag);
        if (!seg.SideHasConnection(tag.Side)) return;

        RemoveWall(level, seg.GetSide(tag.Side).Wall);
        // Remove the connection from the other side
        auto otherId = level.GetConnectedSide(tag);
        if (auto other = level.TryGetSegment(otherId)) {
            RemoveWall(level, other->GetSide(otherId.Side).Wall);
            other->GetConnection(otherId.Side) = SegID::None;
        }

        seg.GetConnection(tag.Side) = SegID::None;
    }

    // Detaches a segment side
    void DetachSide(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return;
        auto& seg = level.GetSegment(tag);
        if (!seg.SideHasConnection(tag.Side)) return;

        auto indices = seg.GetVertexIndicesRef(tag.Side);
        auto start = (uint16)level.Vertices.size();
        for (int i = 0; i < 4; i++)
            level.Vertices.push_back(level.Vertices[*indices[i]]);

        for (ushort i = 0; i < 4; i++)
            *indices[i] = start + i;

        BreakConnection(level, tag);
    }

    SegID GetConnectedSegment(Level& level, SegID id) {
        if (auto seg = level.TryGetSegment(id)) {
            for (auto& side : SideIDs) {
                if (seg->SideHasConnection(side))
                    return seg->GetConnection(side);
            }
        }

        return SegID::None;
    }

    // Returns connected segments up to a depth
    List<SegID> GetConnectedSegments(Level& level, SegID start, int maxDepth) {
        Set<SegID> nearby;
        struct SearchTag { SegID Seg; int Depth; };
        Stack<SearchTag> search;
        search.push({ start, 0 });

        while (!search.empty()) {
            SearchTag tag = search.top();
            search.pop();
            if (tag.Depth >= maxDepth) continue;

            auto seg = level.TryGetSegment(tag.Seg);
            if (!seg) continue;

            nearby.insert(tag.Seg);

            for (auto& side : SideIDs) {
                if (seg->SideIsWall(side) && Settings::Selection.StopAtWalls) continue;
                auto conn = seg->GetConnection(side);
                if (conn > SegID::None && !nearby.contains(conn)) {
                    search.push({ conn, tag.Depth + 1 });
                }
            }
        }

        return Seq::ofSet(nearby);
    }

    void DeleteSegment(Level& level, SegID segId) {
        if (level.Segments.size() <= 1) return; // don't delete the last segment
        if (!level.SegmentExists(segId)) return;

        RemoveMatcen(level, level.GetSegment(segId).Matcen);

        // Remove contained objects
        {
            List<ObjID> objects;
            for (int i = 0; i < level.Objects.size(); i++) {
                if (level.Objects[i].Segment == segId)
                    objects.push_back((ObjID)i);
            }

            // reverse sort should keep the object iterator valid
            ranges::sort(objects, ranges::greater());
            for (auto& obj : objects)
                DeleteObject(level, obj);
        }

        // Remove walls and connected walls
        {
            List<WallID> walls;

            // Remove walls on this seg
            for (int16 wallId = 0; wallId < level.Walls.size(); wallId++) {
                if (level.Walls[wallId].Tag.Segment == segId)
                    walls.push_back((WallID)wallId);
            }

            // Also remove any connected walls
            for (auto& sideId : SideIDs) {
                auto conn = level.GetConnectedSide({ segId, sideId });
                auto cwall = level.TryGetWallID(conn);
                if (cwall != WallID::None)
                    walls.push_back(cwall);
            }

            // reverse sort should keep the object iterator valid
            ranges::sort(walls, ranges::greater());
            for (auto& wall : walls)
                RemoveWall(level, wall);
        }

        // Remove all connections
        for (auto& id : SideIDs)
            DetachSide(level, { segId, id });

        // Remove trigger targets pointing at this segment
        for (auto& trigger : level.Triggers) {
            for (int i = (int)trigger.Targets.Count() - 1; i >= 0; i--) {
                if (trigger.Targets[i].Segment == segId)
                    trigger.Targets.Remove(i);
            }
        }

        Editor::Marked.RemoveSegment(segId);

        // Delete the segment
        ShiftSegmentRefs(level, segId, -1);
        Seq::removeAt(level.Segments, (int)segId);
    }

    // Inserts a uniform 20x20 segment centered on the selected face when extrude is false.
    // Uses face normal of length 20 if no offset is provided.
    SegID InsertSegment(Level& level, Tag src, int alignedToVert, InsertMode mode, const Vector3* offset) {
        if (!level.SegmentExists(src.Segment)) return SegID::None;
        auto& srcSeg = level.GetSegment(src.Segment);
        if (srcSeg.SideHasConnection(src.Side)) return SegID::None;

        auto& srcSide = srcSeg.GetSide(src.Side);
        auto srcIndices = srcSeg.GetVertexIndices(src.Side);
        auto vertIndex = (uint16)level.Vertices.size(); // take index before adding new points

        Vector3 normal = offset ? *offset : srcSide.AverageNormal * 20;

        switch (mode) {
            default:
            case InsertMode::Normal:
                CreateOrthoSegmentFace(level, src, alignedToVert, srcIndices, normal);
                break;

            case InsertMode::Extrude:
                // Create the new face on top of the existing one for extrusions
                for (auto& idx : srcIndices) {
                    //level.Vertices.push_back(level.Vertices[idx] - srcSide.AverageNormal * length);
                    level.Vertices.push_back(level.Vertices[idx] - normal);
                }
                break;

            case InsertMode::Mirror:
            {
                // Reflect new points across the original face
                auto verts = level.VerticesForSide(src);
                auto center = AverageVectors(verts);
                Plane plane(center, srcSide.AverageNormal);
                auto reflect = Matrix::CreateReflection(plane);

                auto indices = srcSeg.GetVertexIndices(GetOppositeSide(src.Side));
                for (int i = 3; i >= 0; i--)
                    level.Vertices.push_back(Vector3::Transform(level.Vertices[indices[i]], reflect));
            }
            break;
        }

        // Create segment
        auto oppositeSide = (int)GetOppositeSide(src.Side);
        Segment seg{};
        auto id = (SegID)level.Segments.size();
        seg.Connections[oppositeSide] = src.Segment;
        srcSeg.Connections[(int)src.Side] = id;

        auto& srcVertIndices = SideIndices[oppositeSide];
        auto& destSideIndices = SideIndices[(int)src.Side];

        // Existing face
        seg.Indices[srcVertIndices[3]] = srcIndices[0];
        seg.Indices[srcVertIndices[2]] = srcIndices[1];
        seg.Indices[srcVertIndices[1]] = srcIndices[2];
        seg.Indices[srcVertIndices[0]] = srcIndices[3];

        // New face
        seg.Indices[destSideIndices[0]] = vertIndex + 0;
        seg.Indices[destSideIndices[1]] = vertIndex + 1;
        seg.Indices[destSideIndices[2]] = vertIndex + 2;
        seg.Indices[destSideIndices[3]] = vertIndex + 3;

        // copy textures
        for (int i = 0; i < 6; i++) {
            auto& side = seg.Sides[i];
            side.TMap = srcSeg.Sides[i].TMap;
            side.TMap2 = srcSeg.Sides[i].TMap2;
            side.OverlayRotation = srcSeg.Sides[i].OverlayRotation;
            side.UVs = srcSeg.Sides[i].UVs;

            // Clear door textures
            if (Resources::GetWallClipID(side.TMap) != WClipID::None)
                side.TMap = LevelTexID::Unset;

            if (Resources::GetWallClipID(side.TMap2) != WClipID::None)
                side.TMap2 = LevelTexID::Unset;
        }

        seg.UpdateNormals(level);
        seg.UpdateCenter(level);

        level.Segments.push_back(seg);
        Events::LevelChanged();
        return id;
    }

    SegID AddDefaultSegment(Level& level) {
        Segment seg = {};

        auto offset = (uint16)level.Vertices.size();
        // Back
        level.Vertices.push_back({ 10,  10, -10 });
        level.Vertices.push_back({ 10, -10, -10 });
        level.Vertices.push_back({ -10, -10, -10 });
        level.Vertices.push_back({ -10,  10, -10 });
        // Front
        level.Vertices.push_back({ 10,  10,  10 });
        level.Vertices.push_back({ 10, -10,  10 });
        level.Vertices.push_back({ -10, -10,  10 });
        level.Vertices.push_back({ -10,  10,  10 });

        for (uint16 i = 0; i < 8; i++)
            seg.Indices[i] = offset + i;

        auto isD1 = level.IsDescent1();
        seg.Sides[0].TMap = LevelTexID(isD1 ? 0 : 158);
        seg.Sides[1].TMap = LevelTexID(isD1 ? 271 : 281);
        seg.Sides[2].TMap = LevelTexID(isD1 ? 0 : 158);
        seg.Sides[3].TMap = LevelTexID(isD1 ? 270 : 191);
        seg.Sides[4].TMap = LevelTexID(0);
        seg.Sides[5].TMap = LevelTexID(0);

        seg.UpdateCenter(level);
        seg.UpdateNormals(level);

        Render::LoadTextureDynamic(seg.Sides[0].TMap);
        Render::LoadTextureDynamic(seg.Sides[1].TMap);
        Render::LoadTextureDynamic(seg.Sides[3].TMap);
        Render::LoadTextureDynamic(seg.Sides[4].TMap);
        level.Segments.push_back(std::move(seg));
        auto id = SegID(level.Segments.size() - 1);
        ResetSegmentUVs(level, std::array{ id }, 1, 0);
        Events::LevelChanged();
        return id;
    }

    // Simple approach to aligning sides from OLE but isn't reliable
    void AlignFaces(const Face& src, const Face& dest) {
        float minDist = FLT_MAX;
        int srcVert = 0;
        int dstVert = 0;

        // Find the closest pair of verts on each face
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                auto diff = src[i] - dest[j];
                auto dist = diff.LengthSquared();
                if (dist < minDist) {
                    minDist = dist;
                    srcVert = i;
                    dstVert = j;
                }
            }
        }

        // Align the verts
        for (int i = 0; i < 4; i++)
            src[(srcVert + i) % 4] = dest[(dstVert + (4 - i)) % 4];
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
            return (200.0 * M_PI) / 180.0;
        }
        else {
            auto ratio = dot / (magnitude1 * magnitude2);
            ratio = float((int)(ratio * 1000.0f)) / 1000.0f; // round

            if (ratio < -1.0f || ratio > 1.0f)
                return (199.0 * M_PI) / 180.0;
            else
                return acos(ratio);
        }
    }

    // Returns the angle between the two triangles of a face
    float FlatnessRatio(const Face& face) {
        auto v0 = face[1] - face[0];
        auto v1 = face[2] - face[0]; // across center
        auto v2 = face[3] - face[0];
        v0.Normalize();
        v1.Normalize();
        v2.Normalize();

        auto n0 = v0.Cross(v1);
        auto n1 = v1.Cross(v2);
        n0.Normalize();
        n1.Normalize();
        return AngleBetweenVectors(n0, n1);
    }

    bool SegmentIsDegenerate(Level& level, Segment& seg) {
        // the three adjacent points of a segment for each corner of the segment 
        static const ubyte adjacentPointTable[8][3] = {
            { 1, 3, 4 },
            { 2, 0, 5 },
            { 3, 1, 6 },
            { 0, 2, 7 },
            { 7, 5, 0 },
            { 4, 6, 1 },
            { 5, 7, 2 },
            { 6, 4, 3 }
        };

        for (short nPoint = 0; nPoint < 8; nPoint++) {
            // define vert numbers
            const auto& vert0 = level.Vertices[seg.Indices[nPoint]];
            const auto& vert1 = level.Vertices[seg.Indices[adjacentPointTable[nPoint][0]]];
            const auto& vert2 = level.Vertices[seg.Indices[adjacentPointTable[nPoint][1]]];
            const auto& vert3 = level.Vertices[seg.Indices[adjacentPointTable[nPoint][2]]];

            if (AngleBetweenThreeVectors(vert0, vert1, vert2, vert3) > M_PI_2)
                return true;
            if (AngleBetweenThreeVectors(vert0, vert2, vert3, vert1) > M_PI_2)
                return true;
            if (AngleBetweenThreeVectors(vert0, vert3, vert1, vert2) > M_PI_2)
                return true;
        }

        // also test for flatness
        for (int nSide = 0; nSide < 6; nSide++) {
            auto face = Face::FromSide(level, seg, (SideID)nSide);
            auto flatness = FlatnessRatio(face) * RadToDeg;
            if (flatness > 80)
                return true;
        }

        return false;
    }

    bool JoinSides(Level& level, Tag srcId, Tag destId) {
        if (srcId.Segment == destId.Segment)
            return false;

        if (!level.SegmentExists(srcId) || !level.SegmentExists(destId))
            return false;

        if (level.HasConnection(srcId) || level.HasConnection(destId))
            return false;

        auto& seg = level.GetSegment(srcId);
        auto srcFace = Face::FromSide(level, seg, srcId.Side);
        auto destFace = Face::FromSide(level, destId);

        if (srcFace.SharesIndices(destFace)) {
            AlignFaces(srcFace, destFace);
        }
        else {
            static const std::array forward = { 0, 1, 2, 3 };
            static const std::array reverse = { 3, 2, 1, 0 };
            bool foundValid = false;

            // try attaching the segment to the dest in each orientation until one isn't degenerate
            for (int i = 0; i < 8; i++) {
                auto order = i < 4 ? forward : reverse;

                // copy point locations from dest to dest
                for (int f = 0; f < 4; f++)
                    srcFace[f] = destFace[order[(f + i) % 4]];

                if (!SegmentIsDegenerate(level, seg)) {
                    foundValid = true;
                    break;
                }
            }

            if (!foundValid)
                return false;
        }

        level.TryAddConnection(srcId, destId);
        auto nearby = GetNearbySegments(Game::Level, srcId.Segment);
        WeldVerticesOfOpenSides(Game::Level, nearby, Settings::CleanupTolerance);
        level.UpdateAllGeometricProps();
        return true;
    }

    string OnConnectSegments() {
        if (Editor::Marked.Faces.size() != 1) {
            SetStatusMessageWarn("Exactly one face must be marked to use connect segments");
            return {};
        }

        auto seg = InsertSegment(Game::Level, Editor::Selection.Tag(), 0, InsertMode::Extrude);

        if (JoinSides(Game::Level, { seg, Editor::Selection.Side }, *Editor::Marked.Faces.begin())) {
            Events::LevelChanged();
            Editor::Selection.SetSelection(seg);
            Editor::History.SnapshotSelection();
            return "Connect Segments";
        }
        else {
            DeleteSegment(Game::Level, seg);
            SetStatusMessage("Unable to connect segments");
            return {};
        }
    }

    bool JoinPoints(Level& level, span<PointID> points, Tag dest, short edge) {
        if (!level.SegmentExists(dest)) return false;
        auto& seg = level.GetSegment(dest);
        auto destIndex = seg.GetVertexIndices(dest.Side)[edge % 4];

        List<VertexReplacement> replacements;
        for (auto mark : points)
            replacements.push_back({ mark, destIndex });

        ReplaceVertices(level, replacements);
        level.UpdateAllGeometricProps();
        Events::LevelChanged();
        return true;
    }

    string OnConnectPoints() {
        if (Editor::Marked.Points.empty()) {
            SetStatusMessage("Points must be marked to use Connect Points");
            return {};
        }

        auto points = Seq::ofSet(Marked.Points);

        if (JoinPoints(Game::Level, points, Selection.Tag(), Selection.Point)) {
            Editor::Marked.Points.clear();
            return "Connect Points";
        }

        return {};
    }

    SegID AddSpecialSegment(Level& level, Tag src, SegmentType type, LevelTexID tex) {
        auto id = InsertSegment(level, src, 0, InsertMode::Normal);
        if (id == SegID::None) return id;

        auto& seg = level.GetSegment(id);
        seg.Type = type;

        for (auto& side : seg.Sides) {
            side.TMap = tex;
            side.TMap2 = LevelTexID::Unset;
        }

        Events::TexturesChanged();
        return id;
    }

    // Estimation that treats the sides as planes instead of triangles
    bool PointInSegment(Level& level, SegID id, const Vector3& point) {
        if (!level.SegmentExists(id)) return false;

        for (auto& side : SideIDs) {
            auto face = Face::FromSide(level, id, side);
            if (face.Distance(point) < 0)
                return false;
        }

        return true;
    }

    SegID FindContainingSegment(Level& level, const Vector3& point) {
        for (int id = 0; id < level.Segments.size(); id++) {
            auto& seg = level.GetSegment((SegID)id);
            if (Vector3::Distance(seg.Center, point) > 200) continue;

            if (PointInSegment(level, (SegID)id, point))
                return (SegID)id;
        }

        return SegID::None;
    }

    void Commands::AddEnergyCenter() {
        auto& level = Game::Level;
        auto tag = Editor::Selection.Tag();
        if (level.HasConnection(tag)) return;

        if (level.GetSegmentCount(SegmentType::Energy) + 1 >= level.Limits.FuelCenters) {
            SetStatusMessage("Level already has the maximum number of energy centers");
            return;
        }

        auto tmap = level.IsDescent1() ? LevelTexID(322) : LevelTexID(333);
        Editor::AddSpecialSegment(level, tag, SegmentType::Energy, tmap);

        Editor::History.SnapshotLevel("Add Energy Center");
        Events::LevelChanged();
    }

    void Commands::AddMatcen() {
        auto& level = Game::Level;
        auto tag = Editor::Selection.Tag();
        if (level.HasConnection(tag)) return;

        auto tmap = level.IsDescent1() ? LevelTexID(339) : LevelTexID(361);
        auto id = Editor::AddSpecialSegment(level, tag, SegmentType::Matcen, tmap);
        if (Editor::AddMatcen(level, { id, tag.Side })) {
            Editor::History.SnapshotLevel("Add Matcen");
            Events::LevelChanged();
        }
    }

    void Commands::AddReactor() {
        auto& level = Game::Level;
        auto tag = Editor::Selection.Tag();
        if (level.HasConnection(tag)) return;

        if (Seq::findIndex(Game::Level.Objects, IsReactor)) {
            SetStatusMessageWarn("Level already contains a reactor");
            return;
        }

        auto tmap = level.IsDescent1() ? LevelTexID(337) : LevelTexID(359);
        auto id = Editor::AddSpecialSegment(level, tag, SegmentType::Reactor, tmap);
        Editor::Selection.Segment = id;
        Editor::AddObject(level, { id, tag.Side }, ObjectType::Reactor);

        Editor::History.SnapshotLevel("Add Reactor");
        Events::LevelChanged();
    }

    void Commands::AddSecretExit() {
        auto& level = Game::Level;
        auto tag = Editor::Selection.Tag();
        if (level.HasConnection(tag)) return;

        auto segId = Editor::InsertSegment(level, tag, 0, InsertMode::Normal);

        if (level.IsDescent1()) {
            // Add the exit door
            LevelTexID tmap2{ 444 };
            AddWall(level, tag, WallType::Door, {}, tmap2, WallFlag::DoorLocked);

            auto other = level.GetConnectedSide(tag);
            AddWall(level, other, WallType::Door, {}, tmap2, WallFlag::DoorLocked);
        }
        else {
            // Add the illusionary wall
            LevelTexID tmap{ 426 };
            AddWall(level, tag, WallType::Illusion, tmap, {});

            auto other = level.GetConnectedSide(tag);
            AddWall(level, other, WallType::Illusion, tmap, {});

            auto& seg = level.GetSegment(segId);
            for (auto& side : seg.Sides) side.TMap = tmap;
        }

        // Wall should exist at the original selection now
        auto wall = level.TryGetWallID(tag);

        if (level.IsDescent2()) {
            Editor::AddTrigger(level, wall, TriggerType::SecretExit);
            // Setup the secret exit return
            level.SecretExitReturn = tag.Segment;
            auto face = Face::FromSide(level, tag);

            auto& m = level.SecretReturnOrientation;
            m.Forward(face.AverageNormal());
            m.Right(face.VectorForEdge(Editor::Selection.Point));
            m.Up(m.Forward().Cross(m.Right()));

            UpdateSecretLevelReturnMarker();
        }
        else {
            Editor::AddTrigger(level, wall, TriggerFlagD1::SecretExit);
        }

        Editor::History.SnapshotLevel("Add Secret Exit");
        Events::LevelChanged();
    }

    bool CanAddFlickeringLight(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;
        if (Game::Level.GetFlickeringLight(tag)) return false; // Already have a flickering light

        auto [seg, side] = level.GetSegmentAndSide(tag);
        auto& tmi1 = Resources::GetLevelTextureInfo(side.TMap);
        auto& tmi2 = Resources::GetLevelTextureInfo(side.TMap2);
        return tmi1.Lighting != 0 || tmi2.Lighting != 0;
    }

    bool IsSecretExit(const Trigger& trigger) {
        if (Game::Level.IsDescent1())
            return trigger.HasFlag(TriggerFlagD1::SecretExit);
        else
            return trigger.Type == TriggerType::SecretExit;
    }

    bool IsExit(const Trigger& trigger) {
        if (Game::Level.IsDescent1())
            return trigger.HasFlag(TriggerFlagD1::Exit);
        else
            return trigger.Type == TriggerType::Exit;
    }

    void SetTextureFromWallClip(Level& level, Tag tag, WClipID id) {
        auto side = level.TryGetSide(tag);
        auto clip = Resources::TryGetWallClip(id);
        if (!side || !clip) return;

        if (clip->NumFrames < 0) return;

        if (clip->UsesTMap1())
            side->TMap = clip->Frames[0];
        else
            side->TMap2 = clip->Frames[0];

        if (auto wall = level.TryGetWall(tag))
            wall->Clip = id;
    }

    bool AddFlickeringLight(Level& level, Tag tag, FlickeringLight light) {
        if (!CanAddFlickeringLight(level, tag)) return false;
        light.Tag = tag;
        level.FlickeringLights.push_back(light);

        // Synchronize lights after adding a new one
        for (auto& fl : level.FlickeringLights)
            fl.Timer = 0;

        return true;
    }

    void Commands::AddFlickeringLight() {
        bool addedLight = false;

        for (auto& tag : GetSelectedFaces()) {
            auto& level = Game::Level;
            if (!CanAddFlickeringLight(level, tag)) return;

            FlickeringLight light{ .Tag = tag, .Mask = FlickeringLight::Defaults::Strobe4, .Delay = 50.0f / 1000.0f };

            if (Editor::AddFlickeringLight(level, tag, light))
                addedLight = true;
        }

        if (addedLight)
            Editor::History.SnapshotLevel("Add Flickering Light");
    }

    void Commands::RemoveFlickeringLight() {
        bool removedLight = false;

        for (auto& tag : GetSelectedFaces()) {
            if (!Game::Level.SegmentExists(tag)) return;

            auto light = Game::Level.GetFlickeringLight(tag);
            auto iter = ranges::find_if(Game::Level.FlickeringLights, [tag](auto x) { return x.Tag == tag; });

            if (iter != Game::Level.FlickeringLights.end()) {
                if (auto seg = Game::Level.TryGetSegment(light->Tag))
                    Render::AddLight(Game::Level, light->Tag, *seg);

                Game::Level.FlickeringLights.erase(iter);
                removedLight = true;
            }
        }

        if (removedLight)
            Editor::History.SnapshotLevel("Delete Flickering Light");
    }

    void Commands::AddDefaultSegment() {
        Editor::AddDefaultSegment(Game::Level);
        Editor::History.SnapshotLevel("Add default segment");
    }

    // Tries to delete a segment. Returns a new selection if possible.
    Tag TryDeleteSegment(Level& level, SegID id) {
        auto seg = level.TryGetSegment(id);
        if (!seg) return{};
        SegID newSeg{};
        SideID newSide{};

        for (auto& c : seg->Connections) {
            if (c == SegID::None) continue;

            newSeg = c;
            auto cside = level.GetConnectedSide(id, c);
            if (cside != SideID::None)
                newSide = cside;

            if (id < newSeg)
                newSeg--;
        }

        DeleteSegment(level, id);
        return { newSeg, newSide };
    }

    void DeleteSegments(Level& level, span<SegID> ids) {
        // Must be deleted in descending order to prevent later ids becoming invalid
        Seq::sortDescending(ids);

        for (auto& segId : ids) {
            DeleteSegment(level, segId);
            Editor::Marked.RemoveSegment(segId);
        }

        PruneVertices(Game::Level);
    }

    // Returns any faces that are not connected to any other segments in the input
    List<Tag> GetBoundary(Level& level, span<SegID> segs) {
        Set<Tag> faces;

        for (auto& seg : segs) {
            if (auto s = level.TryGetSegment(seg)) {
                for (auto& side : SideIDs) {
                    auto conn = s->GetConnection(side);
                    if (conn == SegID::None) continue;

                    // If this side isn't connected to another seg in the selection, add it
                    if (!Seq::contains(segs, conn))
                        faces.insert({ seg, side });
                }
            }
        }

        return Seq::ofSet(faces);
    }

    string OnDetachSegments() {
        Editor::History.SnapshotSelection();
        auto segs = GetSelectedSegments();
        auto faces = GetBoundary(Game::Level, segs);
        if (faces.empty()) return {};

        for (auto& face : faces)
            DetachSide(Game::Level, face);

        auto nearby = GetNearbySegmentsExclusive(Game::Level, segs);
        WeldVerticesOfOpenSides(Game::Level, nearby, Settings::CleanupTolerance);
        Events::LevelChanged();
        return "Detach segments";
    }

    std::array<std::array<SideID, 4>, 6> SidesForSide = { {
        { (SideID)4, (SideID)3, (SideID)5, (SideID)1 },
        { (SideID)2, (SideID)4, (SideID)3, (SideID)5 },
        { (SideID)5, (SideID)3, (SideID)4, (SideID)1 },
        { (SideID)0, (SideID)4, (SideID)2, (SideID)5 },
        { (SideID)2, (SideID)3, (SideID)0, (SideID)1 },
        { (SideID)0, (SideID)3, (SideID)2, (SideID)1 }
    } };

    string OnDetachSides() {
        Editor::History.SnapshotSelection();
        auto faces = GetSelectedFaces();
        auto segs = Seq::map(faces, Tag::GetSegID);

        for (auto& face : faces) {
            if (!Game::Level.SegmentExists(face)) continue;
            auto& seg = Game::Level.GetSegment(face);

            DetachSide(Game::Level, face);
            for (auto adj = 0; adj < 4; adj++) {
                auto adjSide = SidesForSide[(int)face.Side][adj];
                // Skip faces of adjacent segs that are also tagged
                if (!Seq::contains(segs, seg.GetConnection(adjSide)))
                    DetachSide(Game::Level, { face.Segment, adjSide });
            }
        }

        // Weld nearby segments outside the selection
        auto nearby = GetNearbySegmentsExclusive(Game::Level, segs);
        WeldVerticesOfOpenSides(Game::Level, nearby, Settings::CleanupTolerance);

        // Weld segments in the selection
        WeldVerticesOfOpenSides(Game::Level, segs, Settings::CleanupTolerance);

        Events::LevelChanged();
        return "Detach Sides";
    }

    string OnExtrudeSegment() {
        auto newSeg = InsertSegment(Game::Level, Selection.Tag(), Selection.Point, InsertMode::Extrude);
        if (newSeg == SegID::None) return {};

        Selection.SetSelection({ newSeg, Selection.Side });
        auto nearby = GetNearbySegments(Game::Level, newSeg);
        JoinTouchingSegments(Game::Level, Selection.Segment, nearby, Settings::CleanupTolerance);
        return "Extrude Segment";
    }

    string OnInsertSegment() {
        auto newSeg = InsertSegment(Game::Level, Selection.Tag(), Selection.Point, Settings::InsertMode);
        if (newSeg == SegID::None) return {};

        Selection.SetSelection({ newSeg, Selection.Side });
        auto nearby = GetNearbySegments(Game::Level, newSeg);
        JoinTouchingSegments(Game::Level, Selection.Segment, nearby, Settings::CleanupTolerance);
        return "Insert Segment";
    }

    string OnInsertMirroredSegment() {
        auto newSeg = InsertSegment(Game::Level, Selection.Tag(), Selection.Point, InsertMode::Mirror);
        if (newSeg == SegID::None) return {};

        Selection.SetSelection({ newSeg, Selection.Side });
        auto nearby = GetNearbySegments(Game::Level, newSeg);
        JoinTouchingSegments(Game::Level, Selection.Segment, nearby, Settings::CleanupTolerance);
        return "Mirror Segment";
    }

    string OnExtrudeFaces() {
        auto faces = Seq::ofSet(Editor::Marked.Faces);

        // Use the average normals of the marked faces
        int i = 0;
        Vector3 offset;
        for (auto& face : faces) {
            if (auto side = Game::Level.TryGetSide(face)) {
                offset += side->AverageNormal;
                i++;
            }
        }

        if (i == 0) { // In case no faces are valid
            offset = Vector3::Up;
            i = 1;
        }

        offset /= (float)i;
        offset.Normalize();
        offset *= 20;

        auto newSegs = ExtrudeFaces(Game::Level, faces, offset);
        ResetSegmentUVs(Game::Level, newSegs);
        auto segFaces = FacesForSegments(newSegs);
        JoinTouchingSegmentsExclusive(Game::Level, segFaces, Settings::CleanupTolerance);
        return "Extrude Faces";
    }

    string OnJoinSides() {
        if (Editor::Marked.Faces.size() != 1) {
            SetStatusMessageWarn("Exactly one face must be marked to use Join Sides");
            return {};
        }

        auto src = Editor::Selection.Tag();
        Tag dest = *Editor::Marked.Faces.begin();
        auto srcFace = Face::FromSide(Game::Level, src);
        auto destFace = Face::FromSide(Game::Level, dest);
        JoinSides(Game::Level, src, dest);

        Events::LevelChanged();
        return "Join Sides";
    }

    bool MergeSegment(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;

        auto opposite = level.GetConnectedSide(tag);
        if (!level.SegmentExists(opposite)) return false; // no connection

        opposite.Side = GetOppositeSide(opposite.Side);

        for (auto& side : SideIDs) {
            if (side == GetOppositeSide(tag.Side)) continue; // don't detach base side
            DetachSide(Game::Level, { tag.Segment, side });
        }

        // move verts on top of the connected opposite side
        auto oppFace = Face::FromSide(level, opposite);
        auto face = Face::FromSide(level, tag);
        for (int i = 0; i < 4; i++)
            face[i] = oppFace[i];

        DeleteSegment(level, opposite.Segment);
        return true;
    }

    string OnMergeSegment() {
        if (!MergeSegment(Game::Level, Editor::Selection.Tag())) {
            SetStatusMessageWarn("Must select an open side to merge");
            return {};
        }

        Events::LevelChanged();
        return "Merge Segment";
    }

    bool SplitSegment2(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;
        Tag opposite = { tag.Segment, GetOppositeSide(tag.Side) };
        auto connected = level.GetConnectedSide(tag);

        for (auto& side : SideIDs) {
            if (side == opposite.Side) continue;
            DetachSide(Game::Level, { tag.Segment, side });
        }

        auto srcFace = Face::FromSide(level, tag);
        auto oppFace = Face::FromSide(level, opposite);

        // find midpoints of each edge and move. copy original vertices.
        Vector3 midpoints[4]{};
        for (int i = 0; i < 4; i++)
            midpoints[i] = (srcFace[i] + oppFace[3 - i]) / 2;

        auto original = srcFace.CopyPoints();

        for (int i = 0; i < 4; i++)
            srcFace[i] = midpoints[i];

        // Insert new segment and move it to the original position
        auto newid = InsertSegment(level, tag, 0, InsertMode::Extrude, &Vector3::Zero);
        if (!level.SegmentExists(newid)) return false;
        auto newFace = Face::FromSide(level, { newid, tag.Side });
        for (int i = 0; i < 4; i++)
            newFace[i] = original[i];

        // Lazy way to handle UVs
        for (auto& side : SideIDs) {
            if (side == tag.Side || side == opposite.Side) continue;
            ResetUVs(level, { tag.Segment, side });
            ResetUVs(level, { newid, side });
        }

        if (connected) {
            level.GetSegment(newid).GetConnection(tag.Side) = connected.Segment;
            level.GetSegment(connected.Segment).GetConnection(connected.Side) = newid;
            WeldConnection(level, { newid, tag.Side }, Settings::CleanupTolerance);
        }

        level.UpdateAllGeometricProps();
        return true;
    }

    string OnSplitSegment2() {
        if (!SplitSegment2(Game::Level, Editor::Selection.Tag()))
            return {};

        Events::LevelChanged();
        return "Split Segment 2";
    }

    bool SplitSegment5(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;

        for (auto& side : SideIDs)
            DetachSide(level, { tag.Segment, side });

        // Copy verts for each original side
        Array<Array<Vector3, 4>, 6> orig{};
        for (int side = 0; side < 6; side++)
            orig[side] = level.VerticesForSide({ tag.Segment, SideID(side) });

        Tag opposite = { tag.Segment, GetOppositeSide(tag.Side) };
        auto srcFace = Face::FromSide(level, tag);
        auto oppFace = Face::FromSide(level, opposite);
        auto srcCenter = srcFace.Center();
        auto oppCenter = oppFace.Center();

        // inset vertices on each face towards center
        for (int i = 0; i < 4; i++) {
            srcFace[i] = (srcFace[i] + srcCenter) / 2;
            oppFace[i] = (oppFace[i] + oppCenter) / 2;
        }

        List<SegID> newSegs;

        // Insert new segments between center and each side
        for (int side = 0; side < 6; side++) {
            if (SideID(side) == tag.Side || SideID(side) == opposite.Side) continue;
            auto sid = InsertSegment(level, { tag.Segment, SideID(side) }, 0, InsertMode::Extrude);
            auto face = Face::FromSide(level, { sid, SideID(side) });
            for (int i = 0; i < 4; i++)
                face[i] = orig[side][i]; // Copy original verts for the side
            newSegs.push_back(sid);
        }

        auto nearby = GetNearbySegments(level, tag.Segment);
        for (auto& seg : newSegs) JoinTouchingSegments(level, seg, nearby, Settings::CleanupTolerance);

        newSegs.push_back(tag.Segment);
        ResetSegmentUVs(level, newSegs);
        level.UpdateAllGeometricProps();
        return true;
    }

    string OnSplitSegment5() {
        if (!SplitSegment5(Game::Level, Editor::Selection.Tag()))
            return {};

        Events::LevelChanged();
        return "Split Segment 5";
    }

    // Very similar to split 5 except all faces are inset
    bool SplitSegment7(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;

        for (auto& side : SideIDs)
            DetachSide(level, { tag.Segment, side }); // Detach everything

        // Copy verts for each original side
        Array<Array<Vector3, 4>, 6> orig{};
        for (int side = 0; side < 6; side++)
            orig[side] = level.VerticesForSide({ tag.Segment, SideID(side) });

        Tag opposite = { tag.Segment, GetOppositeSide(tag.Side) };
        auto srcFace = Face::FromSide(level, tag);
        auto oppFace = Face::FromSide(level, opposite);
        auto srcCenter = srcFace.Center();
        auto oppCenter = oppFace.Center();
        auto& srcSeg = level.GetSegment(tag);

        // inset vertices on each face towards center
        for (int i = 0; i < 4; i++) {
            srcFace[i] = (srcFace[i] + srcSeg.Center) / 2;
            oppFace[i] = (oppFace[i] + srcSeg.Center) / 2;
        }

        List<SegID> newSegs;

        // Insert new segments between center and each side
        for (int side = 0; side < 6; side++) {
            auto sid = InsertSegment(level, { tag.Segment, SideID(side) }, 0, InsertMode::Extrude);
            auto face = Face::FromSide(level, { sid, SideID(side) });
            for (int i = 0; i < 4; i++)
                face[i] = orig[side][i]; // Copy original verts for the side
            newSegs.push_back(sid);
        }

        auto nearby = GetNearbySegments(level, tag.Segment);
        for (auto& seg : newSegs) JoinTouchingSegments(level, seg, nearby, Settings::CleanupTolerance);

        newSegs.push_back(tag.Segment);
        ResetSegmentUVs(level, newSegs);
        level.UpdateAllGeometricProps();
        return true;
    }

    string OnSplitSegment7() {
        if (!SplitSegment7(Game::Level, Editor::Selection.Tag()))
            return {};

        Events::LevelChanged();
        return "Split Segment 7";
    }

    bool SplitSegment8(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;

        // Detach everything
        for (auto& side : SideIDs)
            DetachSide(level, { tag.Segment, side });

        Tag opposite = { tag.Segment, GetOppositeSide(tag.Side) };
        auto srcFace = Face::FromSide(level, tag);
        auto oppFace = Face::FromSide(level, opposite);

        // Fill a lattice with the top and bottom faces
        Vector3 grid[3][3][3]{};
        grid[0][0][0] = srcFace[0];
        grid[2][0][0] = srcFace[1];
        grid[2][2][0] = srcFace[2];
        grid[0][2][0] = srcFace[3];

        grid[0][0][2] = oppFace[3];
        grid[2][0][2] = oppFace[2];
        grid[2][2][2] = oppFace[1];
        grid[0][2][2] = oppFace[0];

        auto AverageX = [&grid](int y, int z) { grid[1][y][z] = (grid[0][y][z] + grid[2][y][z]) / 2; };
        auto AverageY = [&grid](int x, int z) { grid[x][1][z] = (grid[x][0][z] + grid[x][2][z]) / 2; };
        auto AverageZ = [&grid](int x, int y) { grid[x][y][1] = (grid[x][y][0] + grid[x][y][2]) / 2; };
        auto FillLayerMidpoints = [&](int z) {
            AverageX(0, z); // Top and bottom center
            AverageX(2, z);
            AverageY(0, z); // Left right center
            AverageY(2, z);
            AverageX(1, z); // center
        };

        FillLayerMidpoints(0);
        FillLayerMidpoints(2);

        for (int x = 0; x < 3; x++)
            for (int y = 0; y < 3; y++)
                AverageZ(x, y); // fill middle layer

        List<SegID> newSegs;

        // Insert and move segments
        for (int x = 0; x < 2; x++) {
            for (int y = 0; y < 2; y++) {
                for (int z = 0; z < 2; z++) {
                    auto sid = (x == 0 && y == 0 && z == 0) ? tag.Segment : AddDefaultSegment(level);
                    auto f0 = Face::FromSide(level, { sid, tag.Side });
                    auto f1 = Face::FromSide(level, { sid, opposite.Side });
                    newSegs.push_back(sid);

                    f0[0] = grid[x + 0][y + 0][z + 0];
                    f0[1] = grid[x + 1][y + 0][z + 0];
                    f0[2] = grid[x + 1][y + 1][z + 0];
                    f0[3] = grid[x + 0][y + 1][z + 0];

                    f1[3] = grid[x + 0][y + 0][z + 1];
                    f1[2] = grid[x + 1][y + 0][z + 1];
                    f1[1] = grid[x + 1][y + 1][z + 1];
                    f1[0] = grid[x + 0][y + 1][z + 1];
                }
            }
        }

        auto nearby = GetNearbySegments(level, tag.Segment);
        for (auto& seg : newSegs) JoinTouchingSegments(level, seg, nearby, Settings::CleanupTolerance);

        ResetSegmentUVs(level, newSegs);
        level.UpdateAllGeometricProps();
        return true;
    }

    string OnSplitSegment8() {
        if (!SplitSegment8(Game::Level, Editor::Selection.Tag()))
            return {};

        Events::LevelChanged();
        return "Split Segment 8";
    }

    namespace Commands {
        Command JoinSides{ .SnapshotAction = OnJoinSides, .Name = "Join Sides" };

        Command InsertMirrored{ .SnapshotAction = OnInsertMirroredSegment, .Name = "Insert Mirrored Segment" };
        Command ExtrudeFaces{ .SnapshotAction = OnExtrudeFaces, .Name = "Extrude Faces" };
        Command ExtrudeSegment{ .SnapshotAction = OnExtrudeSegment, .Name = "Extrude Segment" };
        Command InsertSegment{ .SnapshotAction = OnInsertSegment, .Name = "Insert Segment" };

        Command DetachSegments{ .SnapshotAction = OnDetachSegments, .Name = "Detach Segments" };
        Command DetachSides{ .SnapshotAction = OnDetachSides, .Name = "Detach Sides" };
        Command MergeSegment{ .SnapshotAction = OnMergeSegment, .Name = "Merge Segment" };

        Command JoinPoints{ .SnapshotAction = OnConnectPoints, .Name = "Join Points" };
        Command ConnectSides{ .SnapshotAction = OnConnectSegments, .Name = "Connect Sides" };

        Command SplitSegment2{ .SnapshotAction = OnSplitSegment2, .Name = "Split Segment in 2" };
        Command SplitSegment5{ .SnapshotAction = OnSplitSegment5, .Name = "Split Segment in 5" };
        Command SplitSegment7{ .SnapshotAction = OnSplitSegment7, .Name = "Split Segment in 7" };
        Command SplitSegment8{ .SnapshotAction = OnSplitSegment8, .Name = "Split Segment in 8" };
    }
}

