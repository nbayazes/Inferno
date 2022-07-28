#include "pch.h"
#include "Editor.h"
#include "Editor.Geometry.h"
#include "Types.h"
#include "Input.h"
#include "Editor.Texture.h"
#include "Editor.Segment.h"
#include "Editor.Object.h"

#include "vendor/OpenSimplexNoise.h"
#include <random>

namespace Inferno::Editor {
    using Input::SelectionState;

    short GetPairedEdge(Level& level, Tag tag, short point) {
        auto other = level.GetConnectedSide(tag);
        if (!level.SegmentExists(tag) || !other) return 0;

        auto [seg, side] = level.GetSegmentAndSide(tag);
        //auto face = Face::FromSide(level, tag);
        auto srcIndices = seg.GetVertexIndices(tag.Side);
        auto i0 = srcIndices[point % 4];
        auto i1 = srcIndices[(point + 1) % 4];

        auto& otherSeg = level.GetSegment(other);
        auto otherIndices = otherSeg.GetVertexIndices(other.Side);

        for (short i = 0; i < 4; i++) {
            if ((i0 == otherIndices[i] && i1 == otherIndices[(i + 1) % 4]) ||
                (i1 == otherIndices[i] && i0 == otherIndices[(i + 1) % 4]))
                return i;
        }

        return 0;
    }

    void ReplaceVertices(Level& level, span<VertexReplacement> replacements) {
        for (auto& seg : level.Segments)
            for (auto& i : seg.Indices)
                for (auto& [old, newIndex] : replacements)
                    if (i == old) i = newIndex;

        PruneVertices(level);
    };

    // Replaces src verts with dest
    void MergeSides(Level& level, Tag src, Tag dest, float tolerance) {
        auto& srcSeg = level.GetSegment(src.Segment);
        auto& destSeg = level.GetSegment(dest.Segment);

        auto srcFace = Face::FromSide(level, src);
        auto destFace = Face::FromSide(level, dest);
        if (!srcFace.Overlaps(destFace, tolerance)) return; // faces don't overlap
        if (srcFace.AverageNormal().Dot(destFace.AverageNormal()) >= 0) return; // don't merge sides facing the same way
        if (destSeg.GetConnection(dest.Side) > SegID::None) return; // don't merge sides already connected to something
        if (srcSeg.GetConnection(src.Side) > SegID::None) return; // don't merge sides already connected to something

        srcSeg.GetConnection(src.Side) = dest.Segment;
        destSeg.GetConnection(dest.Side) = src.Segment;

        auto& srcIndices = SideIndices[(int)src.Side];
        auto& destIndices = SideIndices[(int)dest.Side];

        List<VertexReplacement> replacements;

        for (int iDest = 0; iDest < 4; iDest++) {
            auto destIndex = destSeg.Indices[destIndices[iDest]];
            auto& destPoint = level.Vertices[destIndex];
            for (int iSrc = 0; iSrc < 4; iSrc++) {
                auto srcIndex = srcSeg.Indices[srcIndices[iSrc]];
                auto& srcPoint = level.Vertices[srcIndex];
                // find which pairs of points overlap
                if (Vector3::Distance(srcPoint, destPoint) < tolerance) {
                    if (srcIndex != destIndex)
                        replacements.push_back({ srcIndex, destIndex }); // Replaces src with dest
                    break;
                }
            }
        }

        ReplaceVertices(level, replacements);
        Events::LevelChanged();
    }

    SideID GetMatchingSide(Level& level, Tag srcId, SegID destId) {
        auto dest = level.TryGetSegment(destId);
        if (!dest) return SideID::None;
        auto srcFace = Face::FromSide(level, srcId);
        auto verts = dest->GetVertices(level);

        for (auto& sideId : SideIDs) {
            if (dest->SideHasConnection(sideId)) continue;
            auto sideFace = Face::FromSide(level, *dest, sideId);

            if (srcFace.Overlaps(sideFace))
                return sideId;
        }

        return SideID::None;
    };

    void JoinTouchingSegments(Level& level, SegID srcId, span<SegID> segIds, float tolerance, bool skipValidation) {
        auto srcSeg = level.TryGetSegment(srcId);
        if (!srcSeg) return;

        if (!skipValidation && srcSeg->GetEstimatedVolume(level) < 10) return; // malformed seg check

        for (auto& srcSideId : SideIDs) {
            auto srcFace = Face::FromSide(level, *srcSeg, srcSideId);
            for (auto& destid : segIds) {
                if (destid == srcId) continue;
                for (auto& destSide : SideIDs)
                    MergeSides(level, { srcId, srcSideId }, { destid, destSide }, tolerance);
            }
        }

        WeldVertices(level, segIds, Settings::CleanupTolerance);
    }

    // Joins all segments nearby to each segment excluding segments in the source
    void JoinTouchingSegmentsExclusive(Level& level, span<Tag> tags, float tolerance) {
        auto segs = Seq::map(tags, Tag::GetSegID);
        auto nearby = GetNearbySegmentsExclusive(level, segs);

        for (auto& tag : tags) {
            if (!level.SegmentExists(tag)) continue;

            auto srcFace = Face::FromSide(level, tag);
            for (auto& destid : nearby) {
                for (auto& destSide : SideIDs) {
                    MergeSides(level, tag, { destid, destSide }, tolerance);
                }
            }
        }

    }

    List<SegID> GetNearbySegments(Level& level, SegID srcId, float distance) {
        List<SegID> nearbySegs;
        auto src = level.TryGetSegment(srcId);
        if (!src) return nearbySegs;

        for (SegID id = {}; (int)id < level.Segments.size(); id++) {
            auto& seg = level.GetSegment(id);
            if (id == srcId) continue;
            auto dist = Vector3::Distance(src->Center, seg.Center);
            if (dist <= distance) nearbySegs.push_back(id);
        }

        return nearbySegs;
    }

    // Gets nearby segments excluding the ones in ids
    List<SegID> GetNearbySegmentsExclusive(Level& level, span<SegID> ids, float distance) {
        Set<SegID> nearby;

        for (auto& id : ids)
            Seq::insert(nearby, GetNearbySegments(level, id, distance));

        for (auto& id : ids)
            nearby.erase(id);

        return Seq::ofSet(nearby);
    }

    void DeleteVertex(Level& level, uint16 index) {
        // Shift indicies down
        for (auto& seg : level.Segments) {
            for (auto& i : seg.Indices) {
                if (i > index) i--;
            }
        }

        Seq::removeAt(level.Vertices, index);
    }

    bool TriedMergingNewSegments = false;

    // Returns list of new segments
    List<SegID> ExtrudeFaces(Level& level, span<Tag> faces, const Vector3& offset) {
        List<Tag> toAdd, toRemove;
        List<SegID> newSegs;
        bool isExtruding = false;

        for (auto& tag : faces) {
            auto newSeg = InsertSegment(level, { tag.Segment, tag.Side }, 0, InsertMode::Extrude, &offset);

            if (newSeg != SegID::None) {
                isExtruding = true;
                TriedMergingNewSegments = false;
                toAdd.push_back({ newSeg, tag.Side });
                toRemove.push_back(tag);
                newSegs.push_back(newSeg);
            }
        }

        // Move the selection from the old to new faces
        Seq::insert(Editor::Marked.Faces, toAdd);
        for (auto& tag : toRemove)
            Editor::Marked.Faces.erase(tag);

        for (auto& seg : toAdd)
            JoinTouchingSegments(level, seg.Segment, newSegs, Settings::CleanupTolerance);

        return newSegs;
    }

    bool BeginExtrude(Level& level) {
        if (Editor::Marked.Faces.empty()) {
            auto newSeg = InsertSegment(level, { Selection.Segment, Selection.Side }, 0, InsertMode::Extrude, &Vector3::Zero); // No length
            if (newSeg != SegID::None) {
                Selection.SetSelection({ newSeg, Selection.Side });
                return true;
            }
        }
        else {
            auto faces = Seq::ofSet(Editor::Marked.Faces);
            return !ExtrudeFaces(level, faces, Vector3::Zero).empty(); // 0 length for mouse based extrudes
        }
        return false;
    }

    // Returns true if the extrude was successful
    bool FinishExtrude(Level& level, const TransformGizmo& gizmo) {
        if (std::abs(gizmo.TotalDelta) <= 0.1f)
            return false;

        auto segs = Seq::map(GetSelectedFaces(), Tag::GetSegID);
        auto faces = FacesForSegments(segs);
        ResetSegmentUVs(level, segs);
        JoinTouchingSegmentsExclusive(level, faces, 0.09f);
        return true;
    }

    void UpdateExtrudes(Level& level, const TransformGizmo& gizmo) {
        if (Editor::Marked.HasSelection(Settings::SelectionMode)) {
            // tries to merge new segments together when extruding multiple at once
            if (std::abs(gizmo.TotalDelta) > 0.1f && !TriedMergingNewSegments) {
                TriedMergingNewSegments = true;
                // Join the new segments if their edges touch
                auto segs = Seq::map(Editor::Marked.Faces, Tag::GetSegID);
                for (auto& seg : segs)
                    JoinTouchingSegments(level, seg, segs, 0.09f, true);
            }
        }
    }

    // Deletes unused vertices. Returns true if any were deleted.
    bool PruneVertices(Level& level) {
        List<PointID> unused;

        auto LevelUsesVertex = [&](int v) {
            for (auto& seg : level.Segments)
                for (auto& i : seg.Indices)
                    if (i == v) return true;
            return false;
        };

        for (PointID v = 0; v < level.Vertices.size(); v++) {
            if (!LevelUsesVertex(v)) unused.push_back(v);
        }

        Seq::sortDescending(unused);
        for (auto& v : unused)
            DeleteVertex(level, v);

        return !unused.empty();
    }

    // Merges overlapping verts
    int WeldVertices(Level& level, span<PointID> src, float tolerance) {
        auto& verts = level.Vertices;

        List<VertexReplacement> replacements;

        // j = i + 1 because i already compares to every value of j
        for (PointID i = 0; i < verts.size(); i++) {
            if (!Seq::contains(src, i)) continue;
            for (PointID j = i + 1; j < verts.size(); j++) {
                if (!Seq::contains(src, j)) continue;
                if (Vector3::Distance(verts[j], verts[i]) <= tolerance)
                    replacements.push_back({ j, i });
            }
        }

        ReplaceVertices(level, replacements);
        return (int)replacements.size();
    }

    void WeldVertices(Level& level, span<SegID> ids, float tolerance) {
        Set<PointID> points;

        for (auto& id : ids) { // compare against every segment in selection
            if (auto seg = level.TryGetSegment(id)) {
                Seq::insert(points, seg->Indices);
            }
        }

        auto list = Seq::ofSet(points);
        WeldVertices(level, list, tolerance);
    }

    bool WeldConnection(Level& level, Tag srcid, float tolerance) {
        auto conn = level.GetConnectedSide(srcid);
        if (!level.SegmentExists(srcid) || !level.SegmentExists(conn)) return false;
        auto& src = level.GetSegment(srcid);
        auto& dest = level.GetSegment(conn);
        auto& verts = level.Vertices;

        bool replaced = false;

        for (auto& i : src.GetVertexIndicesRef(srcid.Side)) {
            for (auto& j : dest.GetVertexIndicesRef(conn.Side)) {
                if (*i == *j) continue;
                if (Vector3::Distance(verts[*i], verts[*j]) <= tolerance) {
                    //SPDLOG_INFO("Replace seg {} vertex {} with {}", srcid.Segment, *i, *j);
                    // Must replace higher indices with lower ones to prevent circular assignment
                    if (*i > *j)
                        *i = *j;
                    else
                        *j = *i;

                    replaced = true;
                }
            }
        }

        return replaced;
    }

    void WeldVerticesOfOpenSides(Level& level, span<SegID> ids, float tolerance) {
        for (auto& id : ids) {
            if (auto seg = level.TryGetSegment(id)) {
                for (auto& side : SideIDs) {
                    if (!seg->SideHasConnection(side)) continue;
                    WeldConnection(level, { id, side }, tolerance);
                }
            }
        }

        PruneVertices(level);
    }

    void ApplyNoise(Level& level, span<PointID> points, float scale, const Vector3& strength, int64 seed) {
        /* std::random_device rd;
         std::mt19937_64 mt(rd());
         std::uniform_int_distribution<int64> dist(std::llround(std::pow(2, 61)), std::llround(std::pow(2, 62)));
         int64 seed = dist(mt);*/
        OpenSimplexNoise::Noise noise(seed);

        for (auto index : points) {
            auto& v = level.Vertices[index];
            auto p = v / scale;
            auto x = (float)noise.eval(0, p.y, p.z) * strength.x;
            auto y = (float)noise.eval(p.x, 0, p.z) * strength.y;
            auto z = (float)noise.eval(p.x, p.y, 0) * strength.z;
            v += { x, y, z };
        }
    }

    // Geometry scaling only applies to one axis at a time.
    // It moves points using linear snapping instead of applying a multiplier.
    // This proves to be more useful by keeping segment sizes at whole values.
    void ApplyGeometryScaling(Level& level, span<PointID> points) {
        auto scale = Editor::Gizmo.DeltaTransform.Translation(); // Scaling stores transform values
        auto dist = scale.Length();
        if (dist == 0) return;
        auto dir = scale; // direction to move points in
        dir.Normalize();
        float growMult = Editor::Gizmo.Grow ? 1.0f : -1.0f; // are we growing or shrinking?
        //SPDLOG_INFO("Dragging {:.2f}, {:.2f}, {:.2f} Grow: {}", g_ScaleGizmo.Scale.x, g_ScaleGizmo.Scale.y, g_ScaleGizmo.Scale.z, g_ScaleGizmo.Grow);

        auto origin = Editor::Gizmo.Transform.Translation();
        bool crossedPlane = true;

        for (auto& v : points) {
            constexpr auto MinimumPlaneDistance = 1.0f;
            if (std::abs(PointToPlaneDistance(level.Vertices[v], origin, dir)) < MinimumPlaneDistance)
                continue; // don't scale point directly on the plane

            // is this point on the left or right of the plane?
            auto relative = level.Vertices[v] - origin;
            relative.Normalize();
            float directionMultiplier = dir.Dot(relative) > 0 ? 1.0f : -1.0f;

            // Move along the dragged axis
            auto offset = level.Vertices[v] + scale * growMult * directionMultiplier;
            auto offsetDir = offset - origin;
            offsetDir.Normalize();
            float planeDist = PointToPlaneDistance(offset, origin, dir * directionMultiplier);
            if (planeDist < MinimumPlaneDistance)
                continue; // don't scale if point would cross the plane

            crossedPlane = false;
            level.Vertices[v] = offset;
        }

        // discard the last increment if no movement happened due to everything crossing plane
        if (crossedPlane)
            Editor::Gizmo.TotalDelta += dist;
    }

    // Move objects contained by the segment after rotating or translating
    void TransformContainedObjects(Level& level, const TransformGizmo& gizmo) {
        if (Settings::SelectionMode == SelectionMode::Segment && gizmo.Mode != TransformMode::Scale) {
            if (Marked.HasSelection(SelectionMode::Segment)) {
                for (auto& obj : level.Objects) {
                    if (Marked.Segments.contains(obj.Segment)) {
                        obj.Transform(gizmo.DeltaTransform);
                        NormalizeObjectVectors(obj);
                    }
                }
            }
            else {
                for (auto& obj : level.Objects) {
                    if (obj.Segment == Selection.Segment) {
                        obj.Transform(gizmo.DeltaTransform);
                        NormalizeObjectVectors(obj);
                    }
                }
            }
        }
    }

    void TransformGeometry(Level& level, const TransformGizmo& gizmo) {
        if (Selection.Segment == SegID::None) return;

        List<PointID> points =
            Marked.HasSelection(Settings::SelectionMode) ?
            Marked.GetVertexHandles(level) :
            Selection.GetVertexHandles(level);

        if (gizmo.Mode == TransformMode::Scale) {
            ApplyGeometryScaling(level, points);
        }
        else {
            for (auto& v : points)
                level.Vertices[v] = Vector3::Transform(level.Vertices[v], gizmo.DeltaTransform);
        }

        TransformContainedObjects(level, gizmo);
        level.UpdateAllGeometricProps();
    }

    void TransformObjects(Level& level, const TransformGizmo& gizmo) {
        for (auto& oid : GetSelectedObjects()) {
            if (auto obj = level.TryGetObject(oid)) {
                obj->Transform(gizmo.DeltaTransform);
                NormalizeObjectVectors(*obj);

                if (obj->Type == ObjectType::SecretExitReturn)
                    level.SecretReturnOrientation = obj->Rotation;

                if (!PointInSegment(level, obj->Segment, obj->Position)) {
                    auto id = FindContainingSegment(level, obj->Position);
                    // Leave the last good ID if nothing contains the object
                    if (id != SegID::None) obj->Segment = id;
                }
            }
        }
    }

    void TransformSelection(Level& level, const TransformGizmo& gizmo) {
        if (gizmo.State != GizmoState::Dragging) return;

        if (Settings::EnableTextureMode) {
            OnTransformTextures(level, gizmo);
            return;
        }

        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
            case SelectionMode::Face:
            case SelectionMode::Edge:
            case SelectionMode::Point:
                TransformGeometry(level, gizmo);
                break;
            case SelectionMode::Object:
                TransformObjects(level, gizmo);
                break;
            case SelectionMode::Transform:
                Editor::UserCSys *= gizmo.DeltaTransform;
                break;
        }
    }

    void Commands::ApplyNoise(float scale, const Vector3& strength, int64 seed) {
        auto points = GetSelectedVertices();
        Editor::ApplyNoise(Game::Level, points, scale, strength, seed);
        Editor::History.SnapshotLevel("Apply Noise");
        Events::LevelChanged();
    }

    void SnapToGrid(Level& level, span<PointID> indices, float snap) {
        for (auto& i : indices) {
            if (!Seq::inRange(level.Vertices, i)) continue;
            auto& vert = level.Vertices[i];
            vert.x = std::round(vert.x / snap) * snap;
            vert.y = std::round(vert.y / snap) * snap;
            vert.z = std::round(vert.z / snap) * snap;
        }

        level.UpdateAllGeometricProps();
    }

    void Commands::SnapToGrid() {
        auto indices = GetSelectedVertices();
        Editor::SnapToGrid(Game::Level, indices, Settings::TranslationSnap);
        Editor::History.SnapshotLevel("Snap To Grid");
        Events::LevelChanged();
    }

    // Joins nearby segment faces that overlap with the selected segment
    string OnJoinTouchingSegments() {
        auto faces = GetSelectedFaces();
        JoinTouchingSegmentsExclusive(Game::Level, faces, Settings::WeldTolerance);
        Events::LevelChanged();
        return "Join Nearby Sides";
    }

    string OnWeldVertices() {
        auto verts = Seq::ofSet(Editor::Marked.Points);
        if (verts.empty()) {
            SetStatusMessageWarn("Must mark vertices to weld");
            return "";
        }

        Editor::WeldVertices(Game::Level, verts, Settings::WeldTolerance);
        Events::LevelChanged();
        return "Weld Vertices";
    }

    string OnMakeCoplanar() {
        auto seg = GetSelectedSegment();
        if (!seg) return {};

        auto face = Face::FromSide(Game::Level, Editor::Selection.Tag());

        for (auto& i : GetSelectedVertices()) {
            if (auto vert = Game::Level.TryGetVertex(i)) {
                *vert = ProjectPointOntoPlane(*vert, face.Center(), face.AverageNormal());
            }
        }

        Game::Level.UpdateAllGeometricProps();
        Events::LevelChanged();
        return "Make Coplanar";
    }

    Dictionary<PointID, List<SegID>> FindUsages(Level& level, span<PointID> points) {
        Dictionary<PointID, List<SegID>> usages;

        for (int i = 0; i < level.Segments.size(); i++) {
            auto& seg = level.GetSegment((SegID)i);

            for (auto& point : points) {
                if (Seq::contains(seg.Indices, point)) {
                    usages[point].push_back((SegID)i);
                }
            }
        }

        return usages;
    }

    bool DetachPoint(Level& level, Segment& seg, PointID point) {
        if (!Seq::inRange(level.Vertices, point)) return false;

        bool found = false;
        for (auto& i : seg.Indices) {
            if (i == point) {
                // Replace the old point with a new one
                i = (uint16)level.Vertices.size();
                found = true;
                break;
            }
        }

        if (!found) return false;

        level.Vertices.push_back(level.Vertices[point]);
        return true;
    }

    bool DetachPoints(Level& level, span<PointID> points) {
        bool changed = false;

        for (auto& [point, segs] : FindUsages(level, points)) {
            if (segs.size() <= 1) continue;

            for (auto& segid : segs) {
                auto& seg = level.GetSegment(segid);

                bool shouldDetach[6]{}; // Detaching modifies indices so we need to check before doing so
                for (auto& side : SideIDs) {
                    if (seg.SideContainsPoint(side, point))
                        shouldDetach[(int)side] = true;
                }

                DetachPoint(level, seg, point);

                for (int i = 0; i < 6; i++) {
                    if (shouldDetach[i]) BreakConnection(level, { segid, (SideID)i });
                }

                changed = true;
            }
        }

        PruneVertices(level);
        return changed;
    }

    string OnDetachPoints() {
        auto points = GetSelectedVertices();
        if (!DetachPoints(Game::Level, points))
            return {};

        Editor::Marked.Points.clear(); // Detaching points invalidates marks
        Game::Level.UpdateAllGeometricProps();
        Events::LevelChanged();
        return "Detach Points";
    }

    namespace Commands {
        Command WeldVertices{ .SnapshotAction = OnWeldVertices, .Name = "Weld Vertices" };
        Command MakeCoplanar{ .SnapshotAction = OnMakeCoplanar, .Name = "Make Coplanar" };
        Command JoinTouchingSegments{ .SnapshotAction = OnJoinTouchingSegments, .Name = "Join Nearby Sides" };
        Command DetachPoints{ .SnapshotAction = OnDetachPoints, .Name = "Detach Points" };
    }
}
