#include "pch.h"
#include "Editor.h"
#include "Editor.Clipboard.h"
#include "Editor.Object.h"
#include "Editor.Segment.h"
#include "Editor.Wall.h"
#include "Graphics/Render.h"

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
            Seq::move(copy.Vertices, verts);

            auto& front = SIDE_INDICES[(int)SideID::Front];
            auto& back = SIDE_INDICES[(int)SideID::Back];

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
            // Break boundary connections
            for (auto& conn : seg.Connections) {
                if (segIdMapping.contains(conn))
                    conn = segIdMapping[conn];
                else
                    conn = SegID::None;
            }

            for (auto& sid : SideIDs) {
                auto& side = seg.GetSide(sid);
                if (auto wall = level.Walls.TryGetWall(side.Wall)) {
                    if (wall->Type != WallType::WallTrigger && seg.Connections[(int)sid] == SegID::None) {
                        // Don't copy walls on the boundary of copied segments
                        side.Wall = WallID::None;
                        continue;
                    }

                    auto wallCopy = *wall;
                    wallCopy.Tag.Segment = segIdMapping[wallCopy.Tag.Segment];
                    side.Wall = (WallID)copy.Walls.size(); // use local copy count as id

                    // Copy triggers
                    if (auto trigger = level.TryGetTrigger(wall->Trigger)) {
                        wallCopy.Trigger = (TriggerID)copy.Triggers.size(); // use local copy count as id
                        copy.Triggers.push_back(*trigger);
                    }

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
        auto triggerOffset = level.Triggers.size();
        auto segIdOffset = (SegID)level.Segments.size();
        auto matcenOffset = level.Matcens.size();
        Seq::move(level.Vertices, copy.Vertices);

        List<SegID> newIds;

        for (auto& seg : copy.Segments) {
            for (auto& v : seg.Indices)
                v += vertexOffset; // adjust indices to the end

            for (auto& conn : seg.Connections) {
                if (conn != SegID::None)
                    conn += segIdOffset;
            }

            auto segId = static_cast<SegID>(level.Segments.size());
            newIds.push_back(segId);

            for (auto& side : seg.Sides) {
                if (Settings::Editor.PasteSegmentWalls) {
                    if (side.Wall != WallID::None) {
                        copy.Walls[static_cast<size_t>(side.Wall)].Tag.Segment = segId;
                    }
                }
                else {
                    side.Wall = WallID::None;
                }

                Render::LoadTextureDynamic(side.TMap);
                Render::LoadTextureDynamic(side.TMap2);
            }

            if (Settings::Editor.PasteSegmentSpecial) {
                if (seg.Matcen != MatcenID::None)
                    seg.Matcen = MatcenID((int)seg.Matcen + matcenOffset);
            }
            else {
                seg.Matcen = MatcenID::None;
                seg.Type = SegmentType::None;
            }

            level.Segments.push_back(std::move(seg));
        }

        if (Settings::Editor.PasteSegmentObjects) {
            for (auto& o : copy.Objects) {
                if (level.Objects.size() >= level.Limits.Objects) {
                    SPDLOG_WARN("Ran out of space for objects!");
                    break;
                }

                o.Segment += segIdOffset;
                level.Objects.push_back(std::move(o));
            }
        }

        if (Settings::Editor.PasteSegmentWalls) {
            for (auto& wall : copy.Walls) {
                if (!level.Walls.CanAdd(wall.Type)) {
                    SPDLOG_WARN("Ran out of space for walls!");
                    break;
                }

                //segment is already correct, but we need to update tag's wall id below
                //wall.Tag.Segment += segIdOffset;
                if (wall.Trigger != TriggerID::None) {
                    auto& trigger = copy.Triggers[(int)wall.Trigger];

                    // Remove any targets that point to segments that don't exist
                    for (int i = (int)trigger.Targets.Count() - 1; i >= 0; i--) {
                        if (!level.SegmentExists(trigger.Targets[i]))
                            trigger.Targets.Remove(i);
                    }

                    level.Triggers.push_back(trigger);

                    auto triggerId = (int)wall.Trigger + triggerOffset;
                    if (triggerId >= level.Limits.Triggers) {
                        SPDLOG_WARN("Ran out of space for triggers!");
                        break;
                    }
                    wall.Trigger = TriggerID(triggerId);
                }
                //updating the wall id of the tag
                auto tag = wall.Tag;
                auto id = level.Walls.Append(std::move(wall));
                level.GetSide(tag).Wall = id;
            }
        }

        if (Settings::Editor.PasteSegmentSpecial) {
            for (auto& matcen : copy.Matcens) {
                matcen.Segment += segIdOffset;
                level.Matcens.push_back(std::move(matcen));
            }
        }

        level.UpdateAllGeometricProps();

        // Weld internal connections
        for (auto& id : newIds)
            for (auto& side : SideIDs)
                WeldConnection(level, { id, side }, 0.01f);

        return newIds;
    }

    void TransformSegmentsToSelection(Level& level, SegmentClipboardData& copy, Tag dest, bool flip = true) {
        // Transform vertices based on the source and dest faces
        auto srcTransform = copy.Reference;
        auto selectionTransform = GetTransformFromSelection(level, dest, SelectionMode::Face);
        auto srcTranslation = copy.Reference.Translation();
        auto positionDelta = selectionTransform.Translation() - srcTranslation;

        // flip transform to place segments on the other side
        srcTransform.Right(-srcTransform.Right());
        srcTransform.Forward(-srcTransform.Forward());

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
        JoinTouchingSides(level, faces, 0.01f); // Join everything nearby
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

    void PasteSegmentsInPlace(Level& level, const SegmentClipboardData& data, bool markSegs) {
        if (data.Segments.empty()) return;
        auto newIds = InsertSegments(level, data);
        Editor::Marked.Segments.clear();
        if (markSegs)
            Seq::insert(Editor::Marked.Segments, newIds);

        if (!newIds.empty())
            JoinTouchingSegments(level, newIds[0], newIds, 0.01f); // try joining the segment we pasted onto
    }

    using DirectX::SimpleMath::Plane;

    void MirrorSelection(SegmentClipboardData& copy, const Plane& plane) {
        if (copy.Segments.empty()) return;

        auto reflection = Matrix::CreateReflection(plane);

        for (auto& v : copy.Vertices)
            v = Vector3::Transform(v, reflection);

        for (auto& o : copy.Objects) {
            o.Transform(reflection);
            o.Rotation.Right(-o.Rotation.Right()); // fix inversion
        }

        for (auto& wall : copy.Walls) {
            auto& side = wall.Tag.Side;
            // swap sides 2/3 and 0/1
            if (side == SideID(0)) {
                side = SideID(1);
                continue;
            }
            if (side == SideID(1)) {
                side = SideID(0);
                continue;
            }
            if (side == SideID(2)) {
                side = SideID(3);
                continue;
            }
            if (side == SideID(3)) {
                side = SideID(2);
                continue;
            }
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

            auto rotateUVs = [&seg](int i, int j) {
                ranges::rotate(seg.Sides[i].UVs, seg.Sides[i].UVs.begin() + j);
                ranges::rotate(seg.Sides[i].Light, seg.Sides[i].Light.begin() + j);
            };

            auto swapUVs = [&seg](int side, int i, int j) {
                std::swap(seg.Sides[side].UVs[i], seg.Sides[side].UVs[j]);
                std::swap(seg.Sides[side].Light[i], seg.Sides[side].Light[j]);
            };

            swapUVs(0, 0, 2);

            rotateUVs(1, 1);
            swapUVs(1, 3, 2); // mirror x
            swapUVs(1, 1, 0);

            swapUVs(2, 3, 1);

            rotateUVs(3, 1);
            swapUVs(3, 1, 2); // mirror y
            swapUVs(3, 3, 0);

            rotateUVs(4, 1);
            swapUVs(4, 3, 2); // mirror x
            swapUVs(4, 1, 0);

            rotateUVs(5, 3);
            swapUVs(5, 3, 2); // mirror x
            swapUVs(5, 1, 0);
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
        obj.Segment = tag.Segment;
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
            // if other side is valid it means this is an open side.
            // check if there is actually a wall here to copy from to avoid copying blank data
            if (level.TryGetWall(otherSide))
                SideClipboard2 = CopySide(level, otherSide);
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

            side->LockLight = data.Side.LockLight;
            side->LightOverride = data.Side.LightOverride;
            side->LightRadiusOverride = data.Side.LightRadiusOverride;
            side->LightPlaneOverride = data.Side.LightPlaneOverride;
            side->DynamicMultiplierOverride = data.Side.DynamicMultiplierOverride;
            side->EnableOcclusion = data.Side.EnableOcclusion;

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

            if (Settings::Editor.EditBothWallSides) {
                auto otherSide = level.GetConnectedSide(id);
                if (otherSide && SideClipboard2)
                    PasteSide(level, otherSide, *SideClipboard2);
            }
        }

        Events::LevelChanged();
    }

    string OnMirrorSegments() {
        auto side = Game::Level.TryGetSide(Editor::Selection.Tag());
        if (!side) return {};

        auto segs = GetSelectedSegments();
        auto copy = CopySegments(Game::Level, segs);
        auto plane = Plane(side->Center, side->AverageNormal);
        MirrorSelection(copy, plane);
        InsertCopiedSegments(Game::Level, copy);
        return "Mirror Segments";
    }

    string OnPasteMirrored() {
        if (!Game::Level.SegmentExists(Editor::Selection.Tag())) return {};

        auto face = Face::FromSide(Game::Level, Editor::Selection.Tag());
        auto normal = face.VectorForEdge(Editor::Selection.Point);

        SegmentClipboardData copy = SegmentClipboard;
        TransformSegmentsToSelection(Game::Level, copy, Editor::Selection.Tag(), true);
        auto plane = Plane(face.Center(), normal);
        MirrorSelection(copy, plane);
        InsertCopiedSegments(Game::Level, copy);
        Editor::Selection.Forward();
        return "Paste Mirrored Segments";
    }

    string Paste() {
        Editor::History.SnapshotSelection();

        switch (Settings::Editor.SelectionMode) {
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
        switch (Settings::Editor.SelectionMode) {
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
        switch (Settings::Editor.SelectionMode) {
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
