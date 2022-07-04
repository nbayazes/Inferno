#include "pch.h"
#include "Editor.h"
#include "Editor.Clipboard.h"
#include "Editor.Object.h"
#include "Editor.Segment.h"
#include "Editor.Wall.h"

namespace Inferno::Editor {

    struct SideClipboardData {
        SegmentSide Side;
        Option<Wall> Wall;
        Option<FlickeringLight> Flicker;
    };

    SegmentClipboardData SegmentClipboard;
    Option<Object> ObjectClipboard;
    Option<SideClipboardData> SideClipboard1, SideClipboard2;

    // Creates a reusable copy of marked segments and their contents.
    // Adjusts all IDs to be 0 based.
    SegmentClipboardData CopySegments(Level& level, span<SegID> segments, bool segmentsOnly) {
        SegmentClipboardData copy;

        auto InsertVertices = [&](Segment& seg) {
            auto offset = (PointID)copy.Vertices.size();
            // copy verts without regard to connections and update seg indices
            auto verts = seg.CopyVertices(level); // grabs front then back

            Seq::move(verts, copy.Vertices);

            auto& front = SideIndices[(int)SideID::Front];
            auto& back = SideIndices[(int)SideID::Back];

            for (int i = 0; i < 4; i++) {
                seg.Indices[i] = offset + front[i];
                seg.Indices[i + 4] = offset + back[i];
            }
        };

        Dictionary<SegID, SegID> segIdMapping; // to map the original IDs to 0 based ids

        for (auto& id : segments) {
            if (auto seg = level.TryGetSegment(id)) {
                auto segCopy = *seg;
                InsertVertices(segCopy);
                segIdMapping[id] = (SegID)copy.Segments.size();

                if (auto matcen = level.TryGetMatcen(segCopy.Matcen)) {
                    segCopy.Matcen = (MatcenID)copy.Matcens.size();
                    copy.Matcens.push_back(*matcen);
                }

                copy.Segments.push_back(std::move(segCopy));
            }
        }

        // Update refs in the copied segments to be 0 based
        for (auto& seg : copy.Segments) {
            for (auto& conn : seg.Connections) {
                if (segIdMapping.contains(conn))
                    conn = segIdMapping[conn];
                else
                    conn = SegID::None;
            }

            for (auto& sid : SideIDs) {
                auto& side = seg.GetSide(sid);
                if (auto wall = level.TryGetWall(side.Wall)) {
                    if (wall->Type == WallType::WallTrigger || wall->Type == WallType::FlyThroughTrigger) {
                        side.Wall = WallID::None;
                        continue; // Don't copy triggers, they need to be set up again by hand
                    }

                    if (seg.Connections[(int)sid] == SegID::None) {
                        side.Wall = WallID::None;
                        continue; // Skip wall if other side wasn't copied
                    }

                    auto wallCopy = *wall;
                    wallCopy.LinkedWall = (WallID)copy.Walls.size();
                    wallCopy.Tag.Segment = segIdMapping[wallCopy.Tag.Segment];
                    side.Wall = wallCopy.LinkedWall;
                    copy.Walls.push_back(std::move(wallCopy));
                }
            }
        }

        if (!segmentsOnly) {
            // Copy objects from the original level to the new segments
            for (auto& obj : level.Objects) {
                if (Seq::contains(segments, obj.Segment)) {
                    Object objCopy = obj;
                    objCopy.Segment = segIdMapping[obj.Segment];
                    copy.Objects.push_back(std::move(objCopy));
                }
            }
        }

        // Update matcen segment
        for (auto& matcen : copy.Matcens)
            matcen.Segment = segIdMapping[matcen.Segment];

        copy.Reference = GetTransformFromSelection(level, Selection.Tag(), SelectionMode::Face);
        return copy;
    }

    // Inserts segments into a level
    List<SegID> InsertSegments(Level& level, SegmentClipboardData copy) {
        auto vertexOffset = (PointID)level.Vertices.size();
        auto wallOffset = level.Walls.size();
        auto segIdOffset = (SegID)level.Segments.size();
        auto matcenOffset = level.Matcens.size();
        Seq::move(copy.Vertices, level.Vertices);

        List<SegID> newIds;

        for (auto& seg : copy.Segments) {
            for (auto& v : seg.Indices)
                v += vertexOffset; // adjust indices to the end

            for (auto& conn : seg.Connections) {
                if (conn != SegID::None)
                    conn += segIdOffset;
            }

            newIds.push_back((SegID)level.Segments.size());

            for (auto& side : seg.Sides) {
                if (side.Wall != WallID::None) {
                    auto woffset = (int)side.Wall + wallOffset;
                    if (woffset >= level.Limits.Walls) {
                        SPDLOG_WARN("Wall id is out of range!");
                        break;
                    }
                    side.Wall = WallID(woffset);
                }
            }

            if (seg.Matcen != MatcenID::None) seg.Matcen = MatcenID((int)seg.Matcen + matcenOffset);
            level.Segments.push_back(std::move(seg));
        }

        for (auto& o : copy.Objects) {
            if (level.Objects.size() >= level.Limits.Objects) {
                SPDLOG_WARN("Ran out of space for objects!");
                break;
            }

            o.Segment += segIdOffset;
            level.Objects.push_back(std::move(o));
        }

        for (auto& wall : copy.Walls) {
            if (level.Walls.size() >= level.Limits.Walls) {
                SPDLOG_WARN("Ran out of space for walls!");
                break;
            }

            wall.Tag.Segment += segIdOffset;
            level.Walls.push_back(std::move(wall));
        }

        for (auto& matcen : copy.Matcens) {
            matcen.Segment += segIdOffset;
            level.Matcens.push_back(std::move(matcen));
        }

        level.UpdateAllGeometricProps();
        if (!newIds.empty()) JoinTouchingSegments(level, newIds[0], newIds, Settings::CleanupTolerance);
        return newIds;
    }

    void TransformSegmentsToSelection(Level& level, SegmentClipboardData& copy, Tag dest, bool flip = true) {
        // Transform vertices based on the source and dest faces
        auto srcTransform = copy.Reference;
        auto selectionTransform = GetTransformFromSelection(level, dest, SelectionMode::Face);
        auto srcTranslation = copy.Reference.Translation();
        auto positionDelta = selectionTransform.Translation() - srcTranslation;

        // change of basis
        Matrix m0 = srcTransform.Invert(), m1 = selectionTransform;
        m0.Translation(Vector3::Zero); // Must remove translations for change of basis to work
        m1.Translation(Vector3::Zero);
        Matrix rotation = m0 * m1;
        if (flip)
            rotation *= Matrix::CreateFromAxisAngle(m1.Right(), 180 * DegToRad); // rotate to face away from the destination normal
        Matrix transform = Matrix::CreateTranslation(-srcTranslation) * rotation * Matrix::CreateTranslation(srcTranslation + positionDelta);

        for (auto& v : copy.Vertices)
            v = Vector3::Transform(v, transform);

        for (auto& o : copy.Objects)
            o.Transform(transform);
    }

    void InsertCopiedSegments(Level& level, const SegmentClipboardData& copy) {
        auto newIds = InsertSegments(level, copy);
        auto faces = FacesForSegments(newIds);
        JoinTouchingSegmentsExclusive(level, faces, 0.01f); // Join everything nearby
        Editor::Marked.Segments.clear();
        Seq::insert(Editor::Marked.Segments, newIds);
        Events::LevelChanged();
    }

    void PasteSegments(Level& level, Tag tag) {
        SegmentClipboardData copy = SegmentClipboard; // copy the clipboard so transforms don't affect the original
        if (copy.Segments.empty()) return;

        TransformSegmentsToSelection(level, copy, tag);
        InsertCopiedSegments(level, copy);
    }

    void PasteSegmentsInPlace(Level& level, const SegmentClipboardData& data) {
        if (data.Segments.empty()) return;
        auto newIds = InsertSegments(level, data);
        Editor::Marked.Segments.clear();
        Seq::insert(Editor::Marked.Segments, newIds);
        if (!newIds.empty())
            JoinTouchingSegments(level, newIds[0], newIds, 0.01f); // try joining the segment we pasted onto
    }

    void MirrorSelection(Level& level, SegmentClipboardData& copy) {
        if (copy.Segments.empty()) return;

        auto selectionTransform = GetTransformFromSelection(level, Selection.Tag(), SelectionMode::Face);
        auto reflectionPlane = DirectX::SimpleMath::Plane(selectionTransform.Translation(), selectionTransform.Forward());
        Matrix reflection = Matrix::CreateReflection(reflectionPlane);

        for (auto& v : copy.Vertices)
            v = Vector3::Transform(v, reflection);

        for (auto& o : copy.Objects) {
            o.Transform(reflection);
            o.Rotation.Right(-o.Rotation.Right()); // fix inversion
        }

        // Reverse face winding and fix the resulting texture mapping
        for (auto& seg : copy.Segments) {
            // this flips normals but changes the side ordering
            std::reverse(seg.Indices.begin(), seg.Indices.begin() + 3);
            std::reverse(seg.Indices.begin() + 4, seg.Indices.begin() + 7);

            // swap sides 2/3 and 0/1
            std::swap(seg.Sides[0], seg.Sides[1]);
            std::swap(seg.Sides[2], seg.Sides[3]);
            std::swap(seg.Connections[0], seg.Connections[1]);
            std::swap(seg.Connections[2], seg.Connections[3]);

            auto RotateUVs = [&seg](int i, int j) {
                std::rotate(seg.Sides[i].UVs.begin(), seg.Sides[i].UVs.begin() + j, seg.Sides[i].UVs.end());
                std::rotate(seg.Sides[i].Light.begin(), seg.Sides[i].Light.begin() + j, seg.Sides[i].Light.end());
            };

            auto SwapUVs = [&seg](int side, int i, int j) {
                std::swap(seg.Sides[side].UVs[i], seg.Sides[side].UVs[j]);
                std::swap(seg.Sides[side].Light[i], seg.Sides[side].Light[j]);
            };

            SwapUVs(0, 0, 2);

            RotateUVs(1, 1);
            SwapUVs(1, 3, 2); // mirror x
            SwapUVs(1, 1, 0);

            SwapUVs(2, 3, 1);

            RotateUVs(3, 1);
            SwapUVs(3, 1, 2); // mirror y
            SwapUVs(3, 3, 0);

            RotateUVs(4, 1);
            SwapUVs(4, 3, 2); // mirror x
            SwapUVs(4, 1, 0);

            RotateUVs(5, 3);
            SwapUVs(5, 3, 2); // mirror x
            SwapUVs(5, 1, 0);
        }
    }

    bool CopyObject(Level& level, ObjID id) {
        if (auto obj = level.TryGetObject(id)) {
            ObjectClipboard = *obj;
            return true;
        }

        return false;
    }

    void PasteObject(Level& level, Tag tag) {
        if (!ObjectClipboard) return;

        if (level.Objects.size() + 1 >= level.Limits.Objects) {
            ShowWarningMessage(L"Out of room for objects!");
            return;
        }

        auto seg = level.TryGetSegment(tag);
        if (!seg) return;

        Object obj = *ObjectClipboard;
        obj.Position = seg->Center;
        level.Objects.push_back(obj);
        Editor::Selection.SetSelection(ObjID(level.Objects.size() - 1));
    }

    Option<SideClipboardData> CopySide(Level& level, Tag tag) {
        if (auto side = level.TryGetSide(tag)) {
            SideClipboardData data{};
            data.Side = *side;
            
            if (auto wall = level.TryGetWall(tag))
                data.Wall = *wall;

            if (auto flicker = level.GetFlickeringLight(tag))
                data.Flicker = *flicker;

            return data;
        }

        return {};
    }

    void OnCopySide(Level& level, Tag tag) {
        SideClipboard1 = CopySide(level, tag);
        if (auto otherSide = level.GetConnectedSide(tag)) {
            SideClipboard2 = CopySide(level, tag);
        }
        else {
            SideClipboard2 = {};
        }
    }

    void PasteSide(Level& level, Tag id, const SideClipboardData& data) {
        if (auto side = level.TryGetSide(id)) {
            side->TMap = data.Side.TMap;
            side->TMap2 = data.Side.TMap2;
            side->OverlayRotation = data.Side.OverlayRotation;

            if (data.Wall)
                AddWall(level, id, data.Wall->Type, side->TMap, side->TMap2, data.Wall->Flags);

            if (data.Flicker)
                AddFlickeringLight(level, id, *data.Flicker);
        }
    }

    void OnPasteSide(Level& level, span<Tag> ids) {
        if (!SideClipboard1) return;

        for (auto& id : ids) {
            PasteSide(level, id, *SideClipboard1);

            auto otherSide = level.GetConnectedSide(id);
            if (otherSide && SideClipboard2)
                PasteSide(level, otherSide, *SideClipboard2);
        }

        Events::LevelChanged();
    }

    string OnMirrorSegments() {
        auto segs = GetSelectedSegments();
        auto copy = CopySegments(Game::Level, segs);
        MirrorSelection(Game::Level, copy);
        InsertCopiedSegments(Game::Level, copy);
        return "Mirror Segments";
    }

    string OnPasteMirrored() {
        SegmentClipboardData copy = SegmentClipboard;
        TransformSegmentsToSelection(Game::Level, copy, Editor::Selection.Tag(), false);
        MirrorSelection(Game::Level, copy);
        InsertCopiedSegments(Game::Level, copy);
        Editor::Selection.Forward();
        return "Paste Mirrored Segments";
    }

    string Paste() {
        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
                PasteSegments(Game::Level, Editor::Selection.Tag());
                return "Paste segments";

            case SelectionMode::Object:
                PasteObject(Game::Level, Editor::Selection.Tag());
                Events::LevelChanged();
                return "Paste objects";

            default:
            {
                auto selection = GetSelectedFaces();
                OnPasteSide(Game::Level, selection);
                return "Paste sides";
            }
        }

        /*SetStatusMessage("Pasted {} segments, {} walls and {} objects",
                         copy.Segments.size(), copy.Walls.size(), copy.Objects.size());*/
    }

    void Copy() {
        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
            {
                auto segs = GetSelectedSegments();
                SegmentClipboard = CopySegments(Game::Level, segs);
                SetStatusMessage("Copied {} segments, {} walls, and {} objects to the clipboard",
                                 SegmentClipboard.Segments.size(), SegmentClipboard.Walls.size(), SegmentClipboard.Objects.size());
                break;
            }

            case SelectionMode::Object:
                if (!CopyObject(Game::Level, Editor::Selection.Object)) {
                    SetStatusMessage("No object selected"); // only happens if no objs in level at all
                    return;
                }

                SetStatusMessage("Copied object");
                break;

            default:
                OnCopySide(Game::Level, Editor::Selection.Tag());
                SetStatusMessage("Copied sides");
                break;
        }
    }

    string Cut() {
        switch (Settings::SelectionMode) {
            case SelectionMode::Segment:
            {
                auto segs = GetSelectedSegments();
                SegmentClipboard = CopySegments(Game::Level, segs);
                DeleteSegments(Game::Level, segs);
                SetStatusMessage("Cut {} segments, {} walls, and {} objects to the clipboard",
                                 SegmentClipboard.Segments.size(), SegmentClipboard.Walls.size(), SegmentClipboard.Objects.size());
                Editor::Marked.Segments.clear();
                Events::LevelChanged();
                return "Cut Segments";
            }

            case SelectionMode::Object:
                if (!CopyObject(Game::Level, Editor::Selection.Object)) {
                    SetStatusMessage("No object selected");
                    return {};
                }

                DeleteObject(Game::Level, Editor::Selection.Object);
                Editor::Marked.Objects.clear();
                return "Cut Objects";

            default: 
                return {};
        }
    }

    namespace Commands {
        Command Cut{ .SnapshotAction = Editor::Cut, .Name = "Cut" };
        Command Copy{ .Action = Editor::Copy, .Name = "Copy" };
        Command Paste{ .SnapshotAction = Editor::Paste, .Name = "Paste" };

        Command MirrorSegments{ .SnapshotAction = OnMirrorSegments, .Name = "Mirror Segments" };
        Command PasteMirrored{ .SnapshotAction = OnPasteMirrored, .Name = "Paste Mirrored" };
    }
}