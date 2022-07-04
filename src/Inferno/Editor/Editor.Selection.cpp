#include "pch.h"
#include "Editor.h"
#include "Graphics/Render.h"
#include "Editor.Segment.h"

namespace Inferno::Editor {
    // Returns true if textures match according to selection settings
    bool TexturesMatch(Level& level, Tag src, Tag tag) {
        auto& s0 = level.GetSide(src);
        auto& s1 = level.GetSide(tag);

        if (Settings::Selection.UseTMap1 && s0.TMap != s1.TMap)
            return false;

        if (Settings::Selection.UseTMap2 && s0.TMap2 != s1.TMap2)
            return false;

        return true;
    }

    List<SelectionHit> HitTestSegments(Level& level, const Ray& ray, bool includeInvisible, SelectionMode mode) {
        List<SelectionHit> hits;
        int segid = 0;
        for (auto& seg : level.Segments) {
            for (auto& side : SideIDs) {
                if (!includeInvisible) {
                    bool visibleWall = false;
                    if (auto wall = level.TryGetWall(seg.Sides[(int)side].Wall))
                        visibleWall = Settings::EnableWallMode || wall->Type != WallType::FlyThroughTrigger;

                    if (seg.SideHasConnection(side) && !visibleWall) continue;
                }

                auto face = Face::FromSide(level, seg, side);
                auto normal = face.AverageNormal();
                if (normal.Dot(ray.direction) > 0) // reject backfacing
                    continue;

                float dist;
                if (face.Intersects(ray, dist)) {
                    auto intersect = ray.position + dist * ray.direction;
                    int16 edge = 0;
                    if (mode == SelectionMode::Point)
                        // find the point on this face closest to the intersect
                        edge = face.GetClosestPoint(intersect);
                    else
                        edge = face.GetClosestEdge(intersect);

                    hits.push_back({ { SegID(segid), side }, edge, normal, dist });
                }
            }

            segid++;
        }

        // Sort by depth
        Seq::sortBy(hits, [](auto& a, auto& b) { return a.Distance < b.Distance; });
        return hits;
    }

    List<SelectionHit> HitTestObjects(Level& level, const Ray& ray) {
        List<SelectionHit> hits;

        for (int id = 0; id < level.Objects.size(); id++) {
            auto& obj = level.Objects[id];
            auto sphere = DirectX::BoundingSphere(obj.Position, obj.Radius);
            if (float dist; ray.Intersects(sphere, dist))
                hits.push_back({ .Distance = dist, .Object = ObjID(id) });
        }

        return hits;
    }

    void EditorSelection::Click(Level& level, Ray ray, SelectionMode mode, bool includeInvisible) {
        List<SelectionHit> hits;

        if (mode == SelectionMode::Object)
            hits = HitTestObjects(level, ray);

        if (hits.empty())
            hits = HitTestSegments(level, ray, includeInvisible, mode);

        Seq::sortBy(hits, [](auto& a, auto& b) { return a.Distance < b.Distance; });

        if (!hits.empty()) {
            // cycle if stack or the topmost hit is the same
            if (Hits == hits || _selection == hits[0]) {
                _cycleDepth++;
                if (_cycleDepth >= hits.size()) _cycleDepth = 0;
            }
            else {
                _cycleDepth = 0;
            }

            _selection = hits[_cycleDepth];

            auto intersectPoint = ray.position + _selection.Distance * ray.direction;

            if (_selection.Tag.HasValue()) {
                Point = _selection.Edge;
            }

            if (_selection.Object != ObjID::None)
                SetSelection(_selection.Object);
            else
                SetSelection(_selection.Tag);
        }
        else {
            Marked.ClearCurrentMode(); // Clear marked if didn't click on anything
            Editor::History.SnapshotSelection();
        }

        Hits = hits;
    }

    // returns the transform origin of the selection
    Vector3 EditorSelection::GetOrigin(SelectionMode mode) const {
        if (!Game::Level.SegmentExists(Segment)) return {};
        auto& segment = Game::Level.GetSegment(Segment);
        auto face = Face::FromSide(Game::Level, segment, Side);

        switch (mode) {
            default:
            case SelectionMode::Segment:
                return segment.Center;

            case SelectionMode::Point:
                return face[Point];

            case SelectionMode::Edge:
            {
                // edge center
                auto p2 = (Point + 1) % 4;
                return (face[Point] + face[p2]) / 2;
            }

            case SelectionMode::Face:
                return face.Center();

            case SelectionMode::Object:
            {
                if (auto obj = Game::Level.TryGetObject(Object))
                    return obj->Position;
                return {};
            }
        }
    }

    // Gets the vertices of the selection
    List<PointID> EditorSelection::GetVertexHandles(Level& level) {
        List<PointID> points;
        if (!Game::Level.SegmentExists(Segment)) return points;
        auto& segment = level.GetSegment(Segment);

        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
            {
                auto segVerts = segment.GetVertices(level);
                auto front = segment.GetVertexIndices(SideID::Front);
                auto back = segment.GetVertexIndices(SideID::Back);
                for (auto& i : front) points.push_back(i);
                for (auto& i : back) points.push_back(i);
                return points;
            }

            case SelectionMode::Point:
            {
                auto indices = segment.GetVertexIndices(Side);
                points.push_back(indices[Point]);
                return points;
            }

            case SelectionMode::Edge:
            {
                auto indices = segment.GetVertexIndices(Side);
                points.push_back(indices[Point]);
                points.push_back(indices[(Point + 1) % 4]);
                return points;
            }

            case SelectionMode::Face:
            {
                auto indices = segment.GetVertexIndices(Side);
                for (auto& i : indices) points.push_back(i);
                return points;
            }

            default:
                return points;
        }
    }

    List<PointID> MultiSelection::GetVertexHandles(Level& level) {
        Set<PointID> points;

        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
                for (auto& id : Segments) {
                    if (const auto& seg = level.TryGetSegment(id)) {
                        points.insert(seg->Indices.begin(), seg->Indices.end());
                    }
                }
                break;

            case SelectionMode::Point:
            case SelectionMode::Edge:
                return { Points.begin(), Points.end() };

            case SelectionMode::Face:
            {
                for (auto& tag : Faces) {
                    if (const auto seg = level.TryGetSegment(tag.Segment)) {
                        for (auto& i : seg->GetVertexIndices(tag.Side))
                            points.insert(i);
                    }
                }
            }
            break;
        }

        return { points.begin(), points.end() };
    }

    List<SegID> MultiSelection::GetSegments(Level& level) {
        Set<SegID> segs;

        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
                for (auto& id : Segments) {
                    if (level.SegmentExists(id))
                        segs.insert(id);
                }
                break;

            case SelectionMode::Point:
            case SelectionMode::Edge:
                // todo: lookup segs for each point in selection
                break;

            case SelectionMode::Face:
            {
                for (auto& tag : Faces) {
                    if (const auto seg = level.TryGetSegment(tag.Segment)) {
                        if (level.SegmentExists(tag.Segment))
                            segs.insert(tag.Segment);
                    }
                }
            }
            break;

            default:
                throw NotImplementedException();
        }

        return { segs.begin(), segs.end() };
    }

    void ToggleElement(auto&& xs, auto id) {
        if (xs.contains(id))
            xs.erase(id);
        else
            xs.insert(id);
    };

    void MultiSelection::MarkAll() {
        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
                for (int i = 0; i < Game::Level.Segments.size(); i++)
                    Segments.insert(SegID(i));
                break;

            case SelectionMode::Edge:
            case SelectionMode::Point:
                for (ushort i = 0; i < Game::Level.Vertices.size(); i++)
                    Points.insert(i);
                break;

            case SelectionMode::Object:
                for (int i = 0; i < Game::Level.Objects.size(); i++)
                    Objects.insert(ObjID(i));
                break;

            case SelectionMode::Face:
                for (int seg = 0; seg < Game::Level.Segments.size(); seg++)
                    for (auto& side : SideIDs)
                        Faces.insert({ (SegID)seg, side });
                break;
        }
    }

    void MultiSelection::ToggleMark() {
        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
                ToggleElement(Segments, Selection.Segment);
                break;

            case SelectionMode::Face:
                ToggleElement(Faces, Selection.Tag());
                break;

            case SelectionMode::Edge:
            {
                auto p1 = Game::Level.IndexForSide(Selection.PointTag());
                auto p2 = Game::Level.IndexForSide({ Selection.Tag(), uint16(Selection.Point + 1) });
                if (!p1 || !p2) return;

                ToggleElement(Points, *p1);
                ToggleElement(Points, *p2);
            }
            break;

            case SelectionMode::Point:
            {
                if (auto p = Game::Level.IndexForSide(Selection.PointTag()))
                    ToggleElement(Points, *p);
            }
            break;

            case SelectionMode::Object:
                ToggleElement(Objects, Selection.Object);
                break;
        }
    }

    void MultiSelection::InvertMarked() {
        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
                for (int seg = 0; seg < Game::Level.Segments.size(); seg++)
                    ToggleElement(Segments, (SegID)seg);
                break;

            case SelectionMode::Face:
                for (int seg = 0; seg < Game::Level.Segments.size(); seg++)
                    for (auto side : SideIDs)
                        ToggleElement(Faces, Tag{ (SegID)seg, side });
                break;

            case SelectionMode::Point:
            case SelectionMode::Edge:
                for (PointID i = 0; i < Game::Level.Vertices.size(); i++)
                    ToggleElement(Points, i);
                break;

            case SelectionMode::Object:
                for (int16 i = 0; i < Game::Level.Objects.size(); i++)
                    ToggleElement(Objects, (ObjID)i);

                break;
        }
    }

    Tuple<LevelTexID, LevelTexID> EditorSelection::GetTextures() {
        if (!Game::Level.SegmentExists(Segment)) return { LevelTexID::None, LevelTexID(0) };

        const auto& seg = Game::Level.GetSegment(Segment);
        return seg.GetTexturesForSide(Side);
    }

    void EditorSelection::SelectByTexture(LevelTexID id) {
        int segId = 0;
        for (auto& seg : Game::Level.Segments) {
            for (auto& side : SideIDs) {
                if (seg.SideHasConnection(side) && !seg.SideIsWall(side)) continue;

                auto& segSide = seg.GetSide(side);
                if (segSide.TMap == id || (segSide.TMap2 == id && segSide.HasOverlay())) {
                    Segment = SegID(segId);
                    Side = side;
                    SPDLOG_INFO("Texture {} used in segment {}:{}", (int)id, segId, side);
                }
            }
            segId++;
        }
    }

    void EditorSelection::SetSelection(Inferno::Tag tag) {
        if (!Game::Level.SegmentExists(tag))
            tag = { SegID(0) };

        Segment = tag.Segment;
        Side = tag.Side;

        Events::SelectSegment();
    }

    void EditorSelection::SelectByTrigger(TriggerID id) {
        for (auto& wall : Game::Level.Walls) {
            if (wall.Trigger == id) {
                Segment = wall.Tag.Segment;
                Side = wall.Tag.Side;
            }
        }
    }
    void EditorSelection::SelectByWall(WallID id) {
        if (auto wall = Game::Level.TryGetWall(id)) {
            Segment = wall->Tag.Segment;
            Side = wall->Tag.Side;
        }
    }

    void EditorSelection::NextItem() {
        switch (Settings::SelectionMode) {
            default:
                NextSide();
                break;
            case SelectionMode::Edge:
            case SelectionMode::Point:
                NextPoint();
                break;
            case SelectionMode::Object:
                break;
        }
    }

    void EditorSelection::PreviousItem() {
        switch (Settings::SelectionMode) {
            default:
                PreviousSide();
                break;
            case SelectionMode::Edge:
            case SelectionMode::Point:
                PreviousPoint();
                break;
            case SelectionMode::Object:
                break;
        }
    }

    void EditorSelection::Forward() {
        if (!Game::Level.SegmentExists(Segment)) return;
        auto& seg = Game::Level.GetSegment(Segment);
        auto next = seg.GetConnection(Side);

        // current side was not connected, search all sides for a connection
        if (next == SegID::None)
            next = GetConnectedSegment(Game::Level, Segment);

        // check side, if open move to connected segment
        if (!Game::Level.SegmentExists(next)) return;

        // Update side to be opposite of the connecting side
        auto& nextSeg = Game::Level.GetSegment(next);
        SideID connectedSide = SideID::None;
        for (auto& side : SideIDs) {
            if (nextSeg.GetConnection(side) == Segment)
                connectedSide = side;
        }

        SetSelection({ next, !connectedSide });
    }

    void EditorSelection::Back() {
        if (Segment == SegID::None) return;
        const auto& seg = Game::Level.GetSegment(Segment);
        auto next = seg.GetConnection(!Side);

        // current side was not connected, search all sides for a connection
        if (next == SegID::None) {
            for (auto& side : SideIDs) {
                if (seg.SideHasConnection(side))
                    next = seg.GetConnection(side);
            }
        }

        // check side, if open move to connected segment
        if (!Game::Level.SegmentExists(next)) return;

        // Update side to be opposite of the connecting side
        auto& nextSeg = Game::Level.GetSegment(next);
        SideID connectedSide = SideID::None;
        for (auto& side : SideIDs) {
            if (nextSeg.GetConnection(side) == Segment)
                connectedSide = side;
        }

        SetSelection({ next, connectedSide });
    }

    void EditorSelection::SelectLinked() {
        auto conn = Game::Level.GetConnectedSide(Tag());
        if (conn.HasValue())
            SetSelection(conn);
    }

    void MultiSelection::Update(Level& level, const Ray& ray) {
        switch (Settings::SelectionMode) {
            case SelectionMode::Face:
            {
                auto hits = HitTestSegments(level, ray, false, Settings::SelectionMode);
                if (hits.empty()) return;
                auto tag = hits[0].Tag;

                if (Input::ControlDown && Input::ShiftDown) {
                    Commands::MarkCoplanar(tag);
                }
                else if (Input::ControlDown) {
                    ToggleElement(Faces, tag);
                }
                else if (Input::ShiftDown) {
                    // if clicked face is selected, remove all faces in the seg
                    if (Faces.contains(tag)) {
                        for (auto& side : SideIDs)
                            Faces.erase(Tag{ tag.Segment, side });
                    }
                    else {
                        for (auto& side : SideIDs) {
                            Tag face = { tag.Segment, side };
                            //if (!TexturesMatch(level, tag, face)) continue;
                            Faces.insert(face);
                        }
                    }
                }
            }
            break;

            case SelectionMode::Segment:
            {
                auto hits = HitTestSegments(level, ray, false, Settings::SelectionMode);
                if (hits.empty()) return;

                if (Input::ControlDown && Input::ShiftDown) {
                    auto segs = GetConnectedSegments(level, hits[0].Tag.Segment, 1000);

                    for (auto& seg : segs)
                        ToggleElement(Segments, seg);
                }
                else {
                    ToggleElement(Segments, hits[0].Tag.Segment);
                }
            }
            break;

            case SelectionMode::Edge:
            {
                auto hits = HitTestSegments(level, ray, false, Settings::SelectionMode);
                if (hits.empty()) return;

                auto intersectPoint = ray.position + hits[0].Distance * ray.direction;
                auto face = Face::FromSide(level, hits[0].Tag);
                auto point = face.GetClosestEdge(intersectPoint);

                if (Input::ShiftDown && Input::ControlDown) {
                    // Remove points of edge
                    Points.erase(face.Indices[point % 4]);
                    Points.erase(face.Indices[(point + 1) % 4]);
                }
                else if (Input::ShiftDown) {
                    // Select all points in segment
                    auto& seg = level.GetSegment(hits[0].Tag);
                    Seq::insert(Marked.Points, seg.Indices);
                }
                else {
                    // Control: Add points of edge. This does not toggle because it would be awkward to mark two adjacent edges.
                    Points.insert(face.Indices[point % 4]);
                    Points.insert(face.Indices[(point + 1) % 4]);
                }
            }
            break;

            case SelectionMode::Point:
            {
                if (Input::ShiftDown) {
                    auto hits = HitTestSegments(level, ray, false, Settings::SelectionMode);
                    if (hits.empty()) return;
                    auto& seg = level.GetSegment(hits[0].Tag);

                    if (Input::ControlDown) {
                        // Toggle segment points when Shift + Control is down
                        for (auto& i : seg.Indices) {
                            if (Marked.Points.contains(i)) Marked.Points.erase(i);
                            else Marked.Points.insert(i);
                        }
                    }
                    else {
                        // Select all points in segment
                        Seq::insert(Marked.Points, seg.Indices);
                    }
                }
                else { // Control
                    auto maxDist = FLT_MAX;
                    Option<PointID> point;

                    // Iterate every level vertex and pick one using hit test + radius
                    for (PointID i = 0; i < level.Vertices.size(); i++) {
                        DirectX::BoundingSphere bounds(level.Vertices[i], 2.5);
                        if (float dist; ray.Intersects(bounds, dist) && dist < maxDist) {
                            maxDist = dist;
                            point = i;
                        }
                    }

                    if (point)
                        ToggleElement(Points, *point);
                }
            }
            break;

            case SelectionMode::Object:
            {
                auto hits = HitTestObjects(level, ray);
                if (hits.empty()) return;
                ToggleElement(Objects, hits[0].Object);
            }
        }

        Editor::History.SnapshotSelection();
    }

    // Returns true if b is between a and c
    constexpr bool Between(float a, float b, float c) {
        return a < c ? (a < b) && (b < c) : (c < b) && (b < a);
    }

    void MultiSelection::UpdateFromWindow(Level& level, Vector2 p0, Vector2 p1, const Camera& camera) {
        auto MarkOrUnmark = [&](const Vector3& pos, auto&& collection, auto val) {
            if (Between(p0.x, pos.x, p1.x) && Between(p0.y, pos.y, p1.y)) {
                if (Input::ShiftDown)
                    collection.erase(val);
                else
                    collection.insert(val);
            }
        };

        auto frustum = Render::Camera.GetFrustum();

        switch (Settings::SelectionMode) {
            default:
            case SelectionMode::Segment:
            {
                for (SegID i{}; (int)i < level.Segments.size(); i++) {
                    auto& seg = level.GetSegment(i);
                    if (!frustum.Contains(seg.Center)) continue;
                    auto vscreen = camera.Project(seg.Center, Matrix::Identity);
                    MarkOrUnmark(vscreen, Segments, i);
                }
                break;
            }
            case SelectionMode::Face:
            {
                for (SegID i{}; (int)i < level.Segments.size(); i++) {
                    auto& seg = level.GetSegment(i);

                    for (auto& side : SideIDs) {
                        auto face = Face::FromSide(level, seg, side);
                        if (!frustum.Contains(face.Center())) continue;
                        auto vscreen = camera.Project(face.Center(), Matrix::Identity);
                        MarkOrUnmark(vscreen, Faces, Tag{ i, side });
                    }
                }
                break;
            }
            case SelectionMode::Edge:
            case SelectionMode::Point:
            {
                for (PointID i = 0; i < level.Vertices.size(); i++) {
                    auto& v = level.Vertices[i];
                    if (!frustum.Contains(v)) continue;
                    auto vscreen = camera.Project(v, Matrix::Identity);
                    MarkOrUnmark(vscreen, Points, i);
                }
                break;
            }
            case SelectionMode::Object:
            {
                for (int i = 0; i < level.Objects.size(); i++) {
                    auto& obj = level.Objects[i];
                    auto pos = obj.Position;
                    if (!frustum.Contains(pos)) continue;
                    auto vscreen = camera.Project(pos, Matrix::Identity);
                    MarkOrUnmark(vscreen, Objects, (ObjID)i);
                }
                break;
            }
        }

        Editor::History.SnapshotSelection();
    }

    // check if any of the sides with these edges are walls
    bool EdgeHasWall(Level& level, Segment& seg, PointID v0, PointID v1) {
        for (auto& sid : SideIDs) {
            auto wall = level.TryGetWall(seg.GetSide(sid).Wall);
            if (!wall) continue;
            if (wall->Type == WallType::FlyThroughTrigger) continue;

            for (int16 edge = 0; edge < 4; edge++) {
                auto src0 = seg.GetVertexIndex(sid, edge);
                auto src1 = seg.GetVertexIndex(sid, edge + 1);

                if ((v0 == src0 && v1 == src1) ||
                    (v0 == src1 && v1 == src0))
                    return true;
            }
        }

        return false;
    }

    // Finds all faces sharing two points with the source face
    Set<Tag> FindTouchingFaces(Level& level, Tag src) {
        Set<Tag> faces;
        if (!level.SegmentExists(src.Segment)) return faces;
        auto& srcSeg = level.GetSegment(src.Segment);

        auto nearby = GetConnectedSegments(level, src.Segment);

        for (int16 srcEdge = 0; srcEdge < 4; srcEdge++) {
            auto src0 = srcSeg.GetVertexIndex(src.Side, srcEdge);
            auto src1 = srcSeg.GetVertexIndex(src.Side, srcEdge + 1);

            for (auto& segid : nearby) {
                if (!level.SegmentExists(segid)) continue;
                auto& destSeg = level.GetSegment(segid);

                // Scan each face and edge
                for (auto& sid : SideIDs) {
                    if (destSeg.SideHasConnection(sid) && !destSeg.GetSide(sid).HasWall()) continue;

                    for (int16 destEdge = 0; destEdge < 4; destEdge++) {
                        auto dest0 = destSeg.GetVertexIndex(sid, destEdge);
                        auto dest1 = destSeg.GetVertexIndex(sid, destEdge + 1);

                        // Check if the vertices are the same
                        if ((dest0 == src0 && dest1 == src1) ||
                            (dest0 == src1 && dest1 == src0)) {
                            if (Settings::Selection.StopAtWalls &&
                                (EdgeHasWall(level, destSeg, dest0, dest1) ||
                                 EdgeHasWall(level, srcSeg, src0, src1)))
                                continue;

                            faces.insert({ segid, sid });
                            break;
                        }
                    }
                }
            }
        }

        return faces;
    }

    Option<Tuple<int16, int16>> FindSharedEdges(Level& level, Tag src, Tag dest) {
        if (!level.SegmentExists(src.Segment) || !level.SegmentExists(dest.Segment)) return {};
        auto& srcSeg = level.GetSegment(src.Segment);
        auto& destSeg = level.GetSegment(dest.Segment);

        int16 destEdge = 0, srcEdge = 0;
        for (srcEdge = 0; srcEdge < 4; srcEdge++) {
            auto srcIndices = srcSeg.GetVertexIndices(src.Side);
            auto src0 = srcSeg.GetVertexIndex(src.Side, srcEdge);
            auto src1 = srcSeg.GetVertexIndex(src.Side, srcEdge + 1);

            for (destEdge = 0; destEdge < 4; destEdge++) {
                auto destIndices = destSeg.GetVertexIndices(dest.Side);
                auto dest0 = destSeg.GetVertexIndex(dest.Side, destEdge);
                auto dest1 = destSeg.GetVertexIndex(dest.Side, destEdge + 1);

                if ((dest0 == src0 && dest1 == src1) ||
                    (dest0 == src1 && dest1 == src0))
                    break;
            }

            if (destEdge != 4)
                break; // found a match in the above loop
        }

        if (srcEdge == 4 || destEdge == 4)
            return {}; // Faces don't share an edge

        return { { srcEdge, destEdge } };
    }

    bool HasVisibleTexture(Level& level, Tag tag) {
        if (!level.SegmentExists(tag)) return false;
        auto [seg, side] = level.GetSegmentAndSide(tag);

        if (auto wall = level.TryGetWall(side.Wall)) {
            return wall->Type != WallType::FlyThroughTrigger;
        }

        return !seg.SideHasConnection(tag.Side);
    }

    void MarkCoplanar(Level& level, Tag tag, bool toggle, Set<Tag>& marked) {
        Set<Tag> visited; // only visit each side once
        Stack<Tag> search;
        search.push(tag);

        while (!search.empty()) {
            Tag src = search.top();
            search.pop();
            visited.insert(src);

            if (toggle)
                marked.erase(src);
            else
                marked.insert(src);

            auto seg = level.TryGetSegment(src.Segment);
            if (!seg) continue;

            for (int connSide = 0; connSide < 6; connSide++) {
                auto conn = seg->Connections[connSide];
                if (!level.SegmentExists(conn)) continue;

                for (auto& dest : FindTouchingFaces(level, src)) {
                    if (!HasVisibleTexture(level, dest)) continue;
                    if (!TexturesMatch(level, src, dest)) continue;

                    auto f0 = Face::FromSide(level, src);
                    auto f1 = Face::FromSide(level, dest);
                    auto angle = AngleBetweenVectors(f0.AverageNormal(), f1.AverageNormal()) * RadToDeg;
                    if (angle < Settings::Selection.PlanarTolerance && !visited.contains(dest))
                        search.push(dest);
                }
            }
        }
    }

    void Commands::MarkCoplanar(Tag tag) {
        try {
            if (!Input::ShiftDown)
                Editor::Marked.Faces.clear();

            bool toggle = Input::ControlDown && Editor::Marked.Faces.contains(tag);
            Editor::MarkCoplanar(Game::Level, tag, toggle, Editor::Marked.Faces);
            Editor::History.SnapshotSelection();
        }
        catch (const std::exception& e) {
            ShowErrorMessage(e);
        }
    }

    void Commands::SelectTexture(bool usePrimary, bool useSecondary) {
        auto& level = Game::Level;
        auto srcSide = level.TryGetSide(Editor::Selection.Tag());
        if (!srcSide) return;

        if (!Input::ShiftDown)
            Editor::Marked.Faces.clear();

        for (int id = 0; id < level.Segments.size(); id++) {
            for (auto& sid : SideIDs) {
                auto& side = level.Segments[id].GetSide(sid);
                bool match = true;

                if (usePrimary) match &= side.TMap == srcSide->TMap;
                if (useSecondary) match &= side.TMap2 == srcSide->TMap2;

                if (match)
                    Editor::Marked.Faces.insert({ (SegID)id, sid });
            }
        }
    }

    namespace Commands {
        Command ToggleMarked{
            .Action = [] {
                Marked.ToggleMark();
                Editor::History.SnapshotSelection();
            },
            .Name = "Toggle Marked"
        };

        Command ClearMarked{
            .Action = [] {
                Marked.Clear();
                Editor::History.SnapshotSelection();
            },
            .Name = "Clear Marked"
        };

        Command MarkAll{
            .Action = [] {
                Marked.MarkAll();
                Editor::History.SnapshotSelection();
            },
            .Name = "Mark All"
        };

        Command InvertMarked{
            .Action = [] {
                Marked.InvertMarked();
                Editor::History.SnapshotSelection();
            },
            .Name = "Invert Marked"
        };
    }
}
