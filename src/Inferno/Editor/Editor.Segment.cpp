#include "pch.h"
#include "Types.h"
#include "Level.h"
#include "Editor.h"
#include "Editor.Segment.h"
#include "Editor.Object.h"
#include "Editor.Wall.h"
#include "Editor.Texture.h"
#include "Graphics/Render.h"
#include "Editor.Diagnostics.h"
#include "Game.Segment.h"
#include "TunnelBuilder.h"

namespace Inferno::Editor {
    void JoinAllTouchingSides(Level& level, span<SegID> segs) {
        level.UpdateAllGeometricProps();

        for (auto& seg : segs) {
            auto faces = FacesForSegment(seg);
            JoinTouchingSides(level, faces, Settings::Editor.CleanupTolerance);
        }
    }

    void RemoveLightDeltasForSegment(Level& level, SegID seg) {
        int16 removed = 0;
        for (auto& index : level.LightDeltaIndices) {
            index.Index -= removed;

            List<int> toRemove;
            for (int i = 0; i < index.Count; i++) {
                if (level.LightDeltas[index.Index + i].Tag.Segment == seg)
                    toRemove.push_back(index.Index + i);
            }

            Seq::sortDescending(toRemove);
            for (auto& i : toRemove)
                Seq::removeAt(level.LightDeltas, i);

            removed += (int16)toRemove.size();
            index.Count -= (uint8)toRemove.size();
        }
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

    bool RemoveFlickeringLight(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;

        auto light = level.GetFlickeringLight(tag);
        auto iter = ranges::find_if(level.FlickeringLights, [tag](auto x) { return x.Tag == tag; });

        if (iter != level.FlickeringLights.end()) {
            if (auto seg = level.TryGetSegment(light->Tag))
                Inferno::AddLight(level, light->Tag, *seg); // restore light on before deleting

            level.FlickeringLights.erase(iter);
            return true;
        }

        return false;
    }

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
        auto shift = [value, ref](SegID& id) {
            if (id >= ref) id += (SegID)value;
        };

        // Update connections
        for (auto& seg : level.Segments) {
            for (auto& c : seg.Connections)
                shift(c);
        }

        // Update object owners
        for (auto& obj : level.Objects)
            shift(obj.Segment);

        for (auto& matcen : level.Matcens)
            shift(matcen.Segment);

        // Update triggers
        for (auto& trigger : level.Triggers) {
            for (auto& target : trigger.Targets)
                shift(target.Segment);
        }

        for (auto& trigger : level.ReactorTriggers)
            shift(trigger.Segment);

        // Update walls
        for (auto& wall : level.Walls)
            shift(wall.Tag.Segment);

        if (TunnelBuilderArgs.Start.Tag.Segment == ref)
            TunnelBuilderArgs.Start = {};
        else
            shift(TunnelBuilderArgs.Start.Tag.Segment);

        if (TunnelBuilderArgs.End.Tag.Segment == ref)
            TunnelBuilderArgs.End = {};
        else
            shift(TunnelBuilderArgs.End.Tag.Segment);

        UpdateTunnelPreview();
    }

    // Creates a 20x20 face aligned to the selected edge and centered to the source face
    void CreateOrthoSegmentFace(Level& level, Tag src, int point, const Array<uint16, 4>& srcIndices, const Vector3& offset) {
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
            auto face = Face::FromSide(level, src);
            auto projectedCenter = face.Center() - offset;
            auto dist = projectedCenter - center;
            for (auto& p : points) p += dist;
        }

        for (auto& p : points)
            level.Vertices.push_back(p);
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
        struct SearchTag {
            SegID Seg;
            int Depth;
        };
        Stack<SearchTag> search;
        search.push({ start, 0 });

        while (!search.empty()) {
            SearchTag tag = search.top();
            search.pop();
            if (tag.Depth > maxDepth) continue;

            auto seg = level.TryGetSegment(tag.Seg);
            if (!seg) continue;

            nearby.insert(tag.Seg);

            for (auto& side : SideIDs) {
                if (seg->SideIsWall(side) && Settings::Editor.Selection.StopAtWalls) continue;
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
            Seq::sortDescending(objects);
            for (auto& obj : objects)
                DeleteObject(level, obj);
        }

        // Remove walls and connected walls
        {
            List<WallID> walls;

            // Remove walls on this seg
            for (int16 wallId = 0; wallId < level.Walls.Size(); wallId++) {
                if (level.Walls[static_cast<WallID>(wallId)].Tag.Segment == segId)
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
            Seq::sortDescending(walls);
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

        RemoveLightDeltasForSegment(level, segId);

        // Remove flickering lights
        for (int i = (int)level.FlickeringLights.size() - 1; i >= 0; i--) {
            if (level.FlickeringLights[i].Tag.Segment == segId)
                RemoveFlickeringLight(level, level.FlickeringLights[i].Tag);
        }

        // Shift remaining light tags
        for (auto& light : level.FlickeringLights) {
            if (light.Tag.Segment > segId)
                light.Tag.Segment--;
        }

        for (auto& light : level.LightDeltas) {
            if (light.Tag.Segment > segId)
                light.Tag.Segment--;
        }

        for (auto& light : level.LightDeltaIndices) {
            if (light.Tag.Segment > segId)
                light.Tag.Segment--;
        }

        Editor::Marked.RemoveSegment(segId);

        // Delete the segment
        ShiftSegmentRefs(level, segId, -1);
        Seq::removeAt(level.Segments, (int)segId);
        Events::SegmentsChanged();
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

        auto& srcVertIndices = SIDE_INDICES[oppositeSide];
        auto& destSideIndices = SIDE_INDICES[(int)src.Side];

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

        seg.UpdateGeometricProps(level);

        level.Segments.push_back(seg);
        return id;
    }

    SegID AddDefaultSegment(Level& level, const Matrix& transform) {
        Segment seg = {};

        std::array verts = {
            // Back
            Vector3{ 10, 10, -10 },
            Vector3{ 10, -10, -10 },
            Vector3{ -10, -10, -10 },
            Vector3{ -10, 10, -10 },
            // Front
            Vector3{ 10, 10, 10 },
            Vector3{ 10, -10, 10 },
            Vector3{ -10, -10, 10 },
            Vector3{ -10, 10, 10 }
        };

        auto offset = (uint16)level.Vertices.size();

        for (auto& v : verts) {
            level.Vertices.push_back(Vector3::Transform(v, transform));
        }

        for (uint16 i = 0; i < 8; i++)
            seg.Indices[i] = offset + i;

        auto isD1 = level.IsDescent1();
        seg.Sides[0].TMap = LevelTexID(isD1 ? 0 : 158);
        seg.Sides[1].TMap = LevelTexID(isD1 ? 271 : 281);
        seg.Sides[2].TMap = LevelTexID(isD1 ? 0 : 158);
        seg.Sides[3].TMap = LevelTexID(isD1 ? 270 : 191);
        seg.Sides[4].TMap = LevelTexID(0);
        seg.Sides[5].TMap = LevelTexID(0);

        seg.UpdateGeometricProps(level);

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

    // Projects a ray from the center of src face to dest face and checks the flatness ratio
    bool RayCheckDegenerate(Level& level, Tag tag) {
        auto opposite = GetOppositeSide(tag.Side);
        auto srcFace = Face::FromSide(level, tag);
        auto destFace = Face::FromSide(level, { tag.Segment, opposite });

        auto vec = destFace.Center() - srcFace.Center();
        auto maxDist = vec.Length();
        vec.Normalize();
        if (vec == Vector3::Zero)
            return true;

        Ray ray(srcFace.Center(), vec);
        for (auto side : SideIDs) {
            if (side == tag.Side || side == opposite)
                continue;

            auto tface = Face::FromSide(level, { tag.Segment, side });
            auto flatness = tface.FlatnessRatio();
            if (flatness <= 0.90f)
                return true;

            float dist{};
            if (tface.Intersects(ray, dist, true) && dist > 0.01f && dist < maxDist) {
                return true;
            }
        }

        return false;
    }

    bool JoinSides(Level& level, Tag srcTag, Tag destId) {
        if (srcTag.Segment == destId.Segment)
            return false;

        if (!level.SegmentExists(srcTag) || !level.SegmentExists(destId))
            return false;

        if (level.HasConnection(srcTag) || level.HasConnection(destId))
            return false;

        auto& seg = level.GetSegment(srcTag);
        auto srcFace = Face::FromSide(level, srcTag);
        auto destFace = Face::FromSide(level, destId);
        auto original = srcFace.CopyPoints();

        static const std::array forward = { 0, 1, 2, 3 };
        static const std::array reverse = { 3, 2, 1, 0 };

        float minCornerAngle = 1000;
        int bestMatch = -1;

        // try attaching the segment to the dest in each orientation until one isn't degenerate
        for (int i = 0; i < 8; i++) {
            auto order = i < 4 ? forward : reverse;

            // copy point locations from dest
            for (int f = 0; f < 4; f++)
                srcFace[f] = destFace[order[(f + i) % 4]];

            seg.UpdateGeometricProps(level);
            auto rayCheck = !RayCheckDegenerate(level, srcTag);
            auto angle = CheckDegeneracy(level, seg);

            if (rayCheck && angle < minCornerAngle) {
                minCornerAngle = angle;
                bestMatch = i;
            }

            // restore location between each iteration because src and dest might share an edge
            for (int f = 0; f < 4; f++)
                srcFace[f] = original[f];
        }

        if (bestMatch == -1) {
            seg.UpdateGeometricProps(level);
            return false;
        }
        else {
            // Move to the best match
            auto order = bestMatch < 4 ? forward : reverse;

            for (int f = 0; f < 4; f++)
                srcFace[f] = destFace[order[(f + bestMatch) % 4]];
        }

        level.TryAddConnection(srcTag, destId);
        auto nearby = GetNearbySegments(Game::Level, srcTag.Segment);
        WeldVerticesOfOpenSides(Game::Level, nearby, Settings::Editor.CleanupTolerance);
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
            std::array segs = { seg };
            auto tags = FacesForSegments(segs);
            JoinTouchingSides(Game::Level, tags, 0.01f);
            ResetSegmentUVs(Game::Level, segs);

            Editor::Selection.SetSelection(seg);
            Events::LevelChanged();
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

        // Clear the overlay texture when it is a secret door
        if (HasFlag(clip->Flags, WallClipFlag::Hidden))
            side->TMap2 = LevelTexID::Unset;

        if (auto wall = level.TryGetWall(tag))
            wall->Clip = id;
    }

    void Commands::AddFlickeringLight() {
        bool addedLight = false;

        for (auto& tag : GetSelectedFaces()) {
            auto& level = Game::Level;
            if (!CanAddFlickeringLight(level, tag)) return; // out of room for lights!

            FlickeringLight light{ .Tag = tag, .Mask = FlickeringLight::Defaults::Strobe4, .Delay = 50.0f / 1000.0f };
            addedLight |= Editor::AddFlickeringLight(level, tag, light);
        }

        if (addedLight) {
            Editor::History.SnapshotSelection();
            Editor::History.SnapshotLevel("Add flickering light");
        }
    }

    void Commands::RemoveFlickeringLight() {
        bool removedLight = false;

        for (auto& tag : GetSelectedFaces())
            removedLight |= Editor::RemoveFlickeringLight(Game::Level, tag);

        if (removedLight) {
            Editor::History.SnapshotSelection();
            Editor::History.SnapshotLevel("Remove flickering light");
        }
    }

    // Tries to delete a segment. Returns a new selection if possible.
    Tag TryDeleteSegment(Level& level, SegID id) {
        auto seg = level.TryGetSegment(id);
        if (!seg) return {};
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

    void DetachSegments(Level& level, span<SegID> segs) {
        auto copy = CopySegments(level, segs);
        DeleteSegments(level, segs);
        PasteSegmentsInPlace(level, copy);

        bool inSelection = false;
        int offset = 0;
        for (auto& seg : segs) {
            if (Editor::Selection.Segment > seg) offset--;
            if (seg == Editor::Selection.Segment)
                inSelection = true;
        }

        if (inSelection) {
            // the selection was in a detached segment, recalculate it
            auto start = SegID(level.Segments.size() - segs.size());
            Editor::Selection.SetSelection(start - (SegID)offset);
        }
        else {
            Editor::Selection.SetSelection(Editor::Selection.Segment + (SegID)offset);
        }
    }

    string OnDetachSegments() {
        Editor::History.SnapshotSelection();
        auto segs = GetSelectedSegments();
        DetachSegments(Game::Level, segs);

        Events::LevelChanged();
        return "Detach segments";
    }

    std::array<std::array<SideID, 4>, 6> SidesForSide = {
        {
            { (SideID)4, (SideID)3, (SideID)5, (SideID)1 },
            { (SideID)2, (SideID)4, (SideID)3, (SideID)5 },
            { (SideID)5, (SideID)3, (SideID)4, (SideID)1 },
            { (SideID)0, (SideID)4, (SideID)2, (SideID)5 },
            { (SideID)2, (SideID)3, (SideID)0, (SideID)1 },
            { (SideID)0, (SideID)3, (SideID)2, (SideID)1 }
        }
    };

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
        WeldVerticesOfOpenSides(Game::Level, nearby, Settings::Editor.CleanupTolerance);

        // Weld segments in the selection
        WeldVerticesOfOpenSides(Game::Level, segs, Settings::Editor.CleanupTolerance);

        Events::LevelChanged();
        return "Detach Sides";
    }

    string OnExtrudeSegment() {
        auto newSeg = InsertSegment(Game::Level, Selection.Tag(), Selection.Point, InsertMode::Extrude);
        if (newSeg == SegID::None) return {};

        Selection.SetSelection({ newSeg, Selection.Side });
        auto nearby = GetNearbySegments(Game::Level, newSeg);
        JoinTouchingSegments(Game::Level, Selection.Segment, nearby, Settings::Editor.CleanupTolerance);
        Events::LevelChanged();
        return "Extrude Segment";
    }

    string OnInsertSegment() {
        auto newSeg = InsertSegment(Game::Level, Selection.Tag(), Selection.Point, Settings::Editor.InsertMode);
        if (newSeg == SegID::None) return {};

        Selection.SetSelection({ newSeg, Selection.Side });
        auto nearby = GetNearbySegments(Game::Level, newSeg);
        JoinTouchingSegments(Game::Level, Selection.Segment, nearby, Settings::Editor.CleanupTolerance);
        Events::LevelChanged();
        return "Insert Segment";
    }

    string OnInsertMirroredSegment() {
        auto newSeg = InsertSegment(Game::Level, Selection.Tag(), Selection.Point, InsertMode::Mirror);
        if (newSeg == SegID::None) return {};

        Selection.SetSelection({ newSeg, Selection.Side });
        auto nearby = GetNearbySegments(Game::Level, newSeg);
        JoinTouchingSegments(Game::Level, Selection.Segment, nearby, Settings::Editor.CleanupTolerance);
        Events::LevelChanged();
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

        if (i == 0) {
            // In case no faces are valid
            offset = Vector3::Up;
            i = 1;
        }

        offset /= (float)i;
        offset.Normalize();
        offset *= 20;

        auto newSegs = ExtrudeFaces(Game::Level, faces, offset);
        ResetSegmentUVs(Game::Level, newSegs);
        auto segFaces = FacesForSegments(newSegs);
        JoinTouchingSides(Game::Level, segFaces, Settings::Editor.CleanupTolerance);
        Events::LevelChanged();
        return "Extrude Faces";
    }

    string OnJoinSides() {
        if (Editor::Marked.Faces.size() != 1) {
            SetStatusMessageWarn("Exactly one face must be marked to Join Sides");
            return {};
        }

        auto src = Editor::Selection.Tag();
        Tag dest = *Editor::Marked.Faces.begin();
        if (src == dest) {
            SetStatusMessageWarn("The marked face must be different than the selected face to Join Sides");
            return {};
        }

        if (!JoinSides(Game::Level, src, dest)) {
            SetStatusMessage("Unable to join sides");
            return {};
        }

        Events::LevelChanged();
        return "Join Sides";
    }

    string MergeSegment(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return "No segment selected";
        auto& seg = level.GetSegment(tag);

        auto opposite = level.GetConnectedSide(tag);
        if (!level.SegmentExists(opposite)) return "Must select an open side to merge segments";

        opposite.Side = GetOppositeSide(opposite.Side);

        for (auto& side : SideIDs) {
            if (side == GetOppositeSide(tag.Side)) continue; // don't detach base side
            DetachSide(Game::Level, { tag.Segment, side });
        }

        // move verts on top of the connected opposite side
        auto endFace = Face::FromSide(level, opposite);
        auto selFace = Face::FromSide(level, tag);

        static const std::array FORWARD = { 0, 1, 2, 3 };
        static const std::array REVERSE = { 3, 2, 1, 0 };
        bool foundValid = false;

        // try attaching the segment to the dest in each orientation until one isn't degenerate
        for (int i = 0; i < 8; i++) {
            //int i = 0;
            auto order = i < 4 ? FORWARD : REVERSE;

            // copy point locations from dest to src
            for (int f = 0; f < 4; f++) {
                selFace[f] = endFace[order[(f + i) % 4]];
                seg.UpdateGeometricProps(level);
            }

            if (!RayCheckDegenerate(level, tag)) {
                foundValid = true;
                break;
            }
        }

        if (!foundValid)
            return "Unable to create valid segment";

        DeleteSegment(level, opposite.Segment);
        return {};
    }

    string OnMergeSegment() {
        auto tag = Editor::Selection.Tag();
        auto opposite = Game::Level.GetConnectedSide(tag);
        auto msg = MergeSegment(Game::Level, tag);
        if (!msg.empty()) {
            SetStatusMessageWarn(msg);
            return {};
        }

        if (opposite.Segment < tag.Segment)
            tag.Segment--;

        Editor::Selection.SetSelection(tag.Segment);
        std::array segs = { tag.Segment };
        auto tags = FacesForSegments(segs);
        JoinTouchingSides(Game::Level, tags, 0.01f);

        Events::LevelChanged();
        return "Merge Segment";
    }

    bool SplitSegment2(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;

        {
            // Detach segment in place and reselect it
            SegID segs[] = { tag.Segment };
            DetachSegments(level, segs);
            tag = Editor::Selection.Tag();
        }

        Tag opposite = { tag.Segment, GetOppositeSide(tag.Side) };
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

        // Reset UVs
        for (auto& side : SideIDs) {
            if (side == tag.Side || side == opposite.Side) continue;
            ResetUVs(level, { tag.Segment, side });
            ResetUVs(level, { newid, side });
        }

        SegID segs[] = { tag.Segment, newid };
        JoinAllTouchingSides(level, segs);

        return true;
    }

    string OnSplitSegment2() {
        if (!SplitSegment2(Game::Level, Editor::Selection.Tag()))
            return {};

        Events::LevelChanged();
        return "Split Segment 2";
    }

    bool SplitSegment3(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;

        {
            // Detach segment in place and reselect it
            SegID segs[] = { tag.Segment };
            DetachSegments(level, segs);
            tag = Editor::Selection.Tag();
        }

        Tag opposite = { tag.Segment, GetOppositeSide(tag.Side) };
        auto srcFace = Face::FromSide(level, tag);
        auto oppFace = Face::FromSide(level, opposite);

        // find midpoints of each edge
        Vector3 midpoints[4]{};
        Vector3 midpoints2[4]{};
        for (int i = 0; i < 4; i++) {
            auto vec = (srcFace[i] - oppFace[3 - i]) / 3;
            midpoints[i] = oppFace[3 - i] + vec;
            midpoints2[i] = oppFace[3 - i] + vec * 2;
        }

        auto endpoints = srcFace.CopyPoints();

        // move the selected face back to the midpoints
        for (int i = 0; i < 4; i++)
            srcFace[i] = midpoints[i];

        // Insert new segment and move it to the second set of midpoints
        auto newid = InsertSegment(level, tag, 0, InsertMode::Extrude, &Vector3::Zero);
        if (!level.SegmentExists(newid)) return false;
        auto newFace = Face::FromSide(level, { newid, tag.Side });
        for (int i = 0; i < 4; i++)
            newFace[i] = midpoints2[i];

        // Insert a second new segment and move it to the end
        auto newid2 = InsertSegment(level, { newid, tag.Side }, 0, InsertMode::Extrude, &Vector3::Zero);
        if (!level.SegmentExists(newid2)) return false;
        auto newFace2 = Face::FromSide(level, { newid2, tag.Side });
        for (int i = 0; i < 4; i++)
            newFace2[i] = endpoints[i];

        // Reset UVs
        for (auto& side : SideIDs) {
            if (side == tag.Side || side == opposite.Side) continue;
            ResetUVs(level, { tag.Segment, side });
            ResetUVs(level, { newid, side });
            ResetUVs(level, { newid2, side });
        }

        SegID segs[] = { tag.Segment, newid, newid2 };
        JoinAllTouchingSides(level, segs);

        return true;
    }

    string OnSplitSegment3() {
        if (!SplitSegment3(Game::Level, Editor::Selection.Tag()))
            return {};

        Events::LevelChanged();
        return "Split Segment 3";
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

        newSegs.push_back(tag.Segment);
        JoinAllTouchingSides(level, newSegs);

        ResetSegmentUVs(level, newSegs);
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

        newSegs.push_back(tag.Segment);
        JoinAllTouchingSides(level, newSegs);
        ResetSegmentUVs(level, newSegs);
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

        auto averageX = [&grid](int y, int z) { grid[1][y][z] = (grid[0][y][z] + grid[2][y][z]) / 2; };
        auto averageY = [&grid](int x, int z) { grid[x][1][z] = (grid[x][0][z] + grid[x][2][z]) / 2; };
        auto averageZ = [&grid](int x, int y) { grid[x][y][1] = (grid[x][y][0] + grid[x][y][2]) / 2; };
        auto fillLayerMidpoints = [&](int z) {
            averageX(0, z); // Top and bottom center
            averageX(2, z);
            averageY(0, z); // Left right center
            averageY(2, z);
            averageX(1, z); // center
        };

        fillLayerMidpoints(0);
        fillLayerMidpoints(2);

        for (int x = 0; x < 3; x++)
            for (int y = 0; y < 3; y++)
                averageZ(x, y); // fill middle layer

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

        newSegs.push_back(tag.Segment);
        JoinAllTouchingSides(level, newSegs);
        ResetSegmentUVs(level, newSegs);
        return true;
    }

    string OnSplitSegment8() {
        if (!SplitSegment8(Game::Level, Editor::Selection.Tag()))
            return {};

        Events::LevelChanged();
        return "Split Segment 8";
    }

    string OnInsertAlignedSegment() {
        auto transform = Editor::Gizmo.Transform;
        if (Editor::Marked.HasSelection(Settings::Editor.SelectionMode)) {
            auto center = Editor::Marked.GetMarkedCenter(Settings::Editor.SelectionMode, Game::Level);
            transform.Translation(center);
        }

        transform.Forward(-transform.Forward()); // flip z axis

        auto id = Editor::AddDefaultSegment(Game::Level, transform);
        Selection.SetSelection(id);
        return "Insert Aligned Segment";
    }

    string OnInsertSegmentAtOrigin() {
        auto id = Editor::AddDefaultSegment(Game::Level);
        Selection.SetSelection(id);
        return "Insert Segment at Origin";
    }

    namespace Commands {
        Command InsertAlignedSegment{ .SnapshotAction = OnInsertAlignedSegment, .Name = "Aligned Segment" };
        Command InsertSegmentAtOrigin{ .SnapshotAction = OnInsertSegmentAtOrigin, .Name = "Segment at Origin" };
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
        Command SplitSegment3{ .SnapshotAction = OnSplitSegment3, .Name = "Split Segment in 3" };
        Command SplitSegment5{ .SnapshotAction = OnSplitSegment5, .Name = "Split Segment in 5" };
        Command SplitSegment7{ .SnapshotAction = OnSplitSegment7, .Name = "Split Segment in 7" };
        Command SplitSegment8{ .SnapshotAction = OnSplitSegment8, .Name = "Split Segment in 8" };
    }
}
