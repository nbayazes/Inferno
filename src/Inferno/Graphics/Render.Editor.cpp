#include "pch.h"
#include "Render.Editor.h"

#include "Game.Boss.h"
#include "Game.Object.h"
#include "Object.h"
#include "Render.h"
#include "Editor/Editor.h"
#include "Render.Debug.h"
#include "Render.Gizmo.h"
#include "Editor/TunnelBuilder.h"
#include "Settings.h"
#include "Editor/UI/EditorUI.h"
#include "Game.Text.h"
#include "Editor/Bindings.h"
#include "Intersect.h"

namespace Inferno::Render {
    void DrawFacingCircle(const Vector3& position, float radius, const Color& color) {
        auto facingMatrix = Matrix::CreateBillboard(position, Camera.Position, Camera.Up);
        Debug::DrawCircle(radius, facingMatrix, color);
    }

    void DrawObjectBoundingBoxes(const Object& object, const Color& color) {
        if (Game::GetState() != GameState::Editor || Settings::Inferno.ScreenshotMode) return;
        if (object.Render.Type != RenderType::Model) return;

        auto& model = Resources::GetModel(object.Render.Model.ID);
        int index = 0;
        auto transform = object.GetTransform(Game::LerpAmount);

        for (auto& submodel : model.Submodels) {
            auto offset = model.GetSubmodelOffset(index++);
            auto world = Matrix::CreateTranslation(offset) * transform;
            auto bounds = submodel.Bounds;
            bounds.Transform(bounds, world);
            Debug::DrawBoundingBox(bounds, color);

            //auto center = Vector3::Transform(offset, transform);
            //auto submodelFacingMatrix = Matrix::CreateBillboard(Vector3::Transform(submodelOffset, objectTransform), Camera.Position, Camera.Up);
            //Debug::DrawCircle(submodel.Radius, submodelFacingMatrix, { 0.1, 0.5, 0.1, 0.50 });
            //DrawFacingCircle(center, submodel.Radius, Color(1, 0, 1));
            //bounds.Center = bounds.Center + submodel.Center;
        }
    }

    void DrawObjectOutline(const Object& object, const Color& color, float scale = 1.0f) {
        if (object.Radius == 0) return;
        if (Game::GetState() != GameState::Editor || Settings::Inferno.ScreenshotMode) return;
        DrawFacingCircle(object.Position, object.Radius * scale, color);
        //DrawObjectBoundingBoxes(object, Color(0, 1, 0));
    }

    void DrawFaceNormals(Level& level) {
        for (auto& seg : level.Segments) {
            for (auto& side : SIDE_IDS) {
                if (seg.SideHasConnection(side)) continue;

                auto face = Face::FromSide(level, seg, side);

                Debug::DrawLine(face.Side.Centers[0], face.Side.Centers[0] + face.Side.Normals[0] * 2.5, Colors::Door);
                Debug::DrawLine(face.Side.Centers[1], face.Side.Centers[1] + face.Side.Normals[1] * 2.5, Colors::Door);
            }
        }
    }

    void DrawObjectOutline(const Object& object) {
        if (object.Radius == 0) return;

        Color color = [&object] {
            switch (object.Type) {
                case ObjectType::Hostage: return Colors::Hostage;
                case ObjectType::Reactor:
                case ObjectType::Robot:
                    return Colors::Robot;
                case ObjectType::Powerup: return Colors::Powerup;
                case ObjectType::Player:
                case ObjectType::Coop:
                    return Colors::Player;
                default: return Color(1, 1, 1);
            }
        }();

        color.A(0.5f);
        DrawFacingCircle(object.Position, object.Radius, color);
    }

    void OutlineBossTeleportSegments() {
        if (!Settings::Editor.OutlineBossTeleportSegments) return;
        for (auto& [segid, pos] : Game::GetTeleportSegments()) {
            if (auto seg = Game::Level.TryGetSegment(segid))
                Render::Debug::OutlineSegment(Game::Level, *seg, Color(0, 1, 0));
        }
    }

    void DrawTunnelPathNode(const Editor::PathNode& node) {
        Matrix m = node.Rotation;
        Vector3 v[4] = { m.Right(), m.Up(), m.Forward(), node.Axis };

        Debug::DrawPoint(node.Position, Colors::MarkedFace);
        static constexpr Color colors[4] = { Colors::DoorRed, Colors::Hostage, Colors::DoorBlue, Colors::DoorGold };

        for (int i = 0; i < 3; i++) {
            Debug::DrawLine(node.Position, node.Position + v[i] * 5, colors[i]);
        }
    }

    void DrawTunnelBuilder(Level& level) {
        //for (int i = 1; i < Editor::TunnelBuilderPath.size(); i++) {
        //    auto& p0 = Editor::TunnelBuilderPath[i - 1];
        //    auto& p1 = Editor::TunnelBuilderPath[i];
        //    Debug::DrawLine(p0, p1, Colors::MarkedFace);
        //}

        //Seq::iteri(Editor::DebugTunnelPoints, [](auto i, auto p) {
        //    static constexpr Color colors[4] = { Colors::DoorRed, Colors::Hostage, Colors::DoorBlue, Colors::DoorGold };
        //    Debug::DrawPoint(p, colors[(i / 4) % 4]);
        //});

        //for (int i = 1; i < Editor::DebugTunnelLines.size(); i += 2) {
        //    auto& p0 = Editor::DebugTunnelLines[i - 1];
        //    auto& p1 = Editor::DebugTunnelLines[i];
        //    Debug::DrawLine(p0, p1, Colors::Fuelcen);
        //}

        // Draw lattice
        for (int n = 1; n < Editor::PreviewTunnel.Nodes.size(); n++) {
            auto& n0 = Editor::PreviewTunnel.Nodes[n - 1];
            auto& n1 = Editor::PreviewTunnel.Nodes[n];

            for (int i = 0; i < 4; i++) {
                Debug::DrawLine(n0.Vertices[i], n1.Vertices[i], Colors::Fuelcen);
                Debug::DrawLine(n1.Vertices[i], n1.Vertices[(i + 1) % 4], Colors::Fuelcen);
            }
        }

        //for (int i = 1; i < Editor::DebugTunnelPoints.size(); i += 2) {
        //    auto& p0 = Editor::DebugTunnelPoints[i - 1];
        //    auto& p1 = Editor::DebugTunnelPoints[i];
        //    if ((i % 4) == 2 || (i % 4) == 3)
        //        Debug::DrawLine(p0, p1, Colors::Hostage);
        //}

        //for (int i = 4; i < Editor::TunnelBuilderPoints.size(); i++) {
        //    auto& p0 = Editor::TunnelBuilderPoints[i - 4];
        //    auto& p1 = Editor::TunnelBuilderPoints[i];
        //    Debug::DrawLine(p0, p1, Colors::Hostage);
        //}

        //for (auto& node : Editor::DebugTunnel.Nodes) {
        //    DrawTunnelPathNode(node);
        //}

        auto drawStartFace = [&](const Editor::TunnelHandle& handle) {
            if (!level.SegmentExists(handle.Tag)) return;
            auto face = Face::FromSide(level, handle.Tag);
            for (int i = 0; i < 4; i++) {
                auto color = handle.Tag.Point == i ? Colors::DoorGold : Colors::MarkedWall;
                Debug::DrawLine(face[i % 4], face[(i + 1) % 4], color);
            }

            auto start = face.Center();
            auto end = start - face.AverageNormal() * handle.Length;
            Debug::DrawLine(start, end, Colors::Reactor);
            Debug::DrawPoint(end, Colors::Reactor);
        };

        drawStartFace(Editor::PreviewTunnelStart);
        drawStartFace(Editor::PreviewTunnelEnd);
    }

    void DrawSelection(const Editor::EditorSelection& selection, const Level& level) {
        using namespace Editor;
        if (!level.SegmentExists(selection.Segment)) return;
        auto& seg = level.GetSegment(selection.Segment);
        auto vs = seg.GetVertices(level);

        Array<Vector4, 12> colors = {};
        auto segColor = Settings::Editor.SelectionMode == SelectionMode::Segment ? Colors::SelectionPrimary : Colors::SelectionOutline;
        colors.fill(segColor);

        auto sideColor = Settings::Editor.SelectionMode == SelectionMode::Face ? Colors::SelectionPrimary : Colors::SelectionTertiary;
        auto& edges = EDGES_OF_SIDE[(int)selection.Side];
        for (int i = 0; i < 4; i++)
            colors[edges[i]] = sideColor;

        auto edgeColor = Settings::Editor.SelectionMode == SelectionMode::Edge ? Colors::SelectionPrimary : Colors::SelectionSecondary;
        colors[edges[selection.Point]] = edgeColor;

        // Draw each of the 12 edges
        for (int i = 0; i < 12; i++) {
            auto& vi = VERTS_OF_EDGE[i];
            auto v1 = vs[vi[0]];
            auto v2 = vs[vi[1]];
            Debug::DrawLine(*v1, *v2, colors[i]);
        }

        auto indices = seg.GetVertexIndices(selection.Side);
        auto& pointPos = level.Vertices[indices[selection.Point]];
        if (Settings::Editor.SelectionMode == SelectionMode::Point)
            DrawFacingCircle(pointPos, 1.5, Colors::SelectionPrimary);
    }

    void DrawUserCSysMarker(ID3D12GraphicsCommandList* cmdList) {
        using namespace Editor;
        auto pos = Editor::UserCSys.Translation();
        auto scale = Matrix::CreateScale(Editor::GetGizmoScale(pos, Camera) * 0.5f);
        auto translation = Matrix::CreateTranslation(pos);

        auto DrawAxis = [&](Vector3 dir) {
            auto rotation = DirectionToRotationMatrix(dir);
            auto transform = rotation * scale * translation * Render::ViewProjection;
            Debug::DrawArrow(cmdList, transform, Colors::GlobalOrientation);
        };

        DrawAxis(Editor::UserCSys.Forward());
        DrawAxis(Editor::UserCSys.Up());
        DrawAxis(Editor::UserCSys.Right());
    }

    void DrawSecretLevelReturn(ID3D12GraphicsCommandList* cmdList, const Matrix& matrix, float size = 1) {
        using namespace Editor;
        auto scale = Matrix::CreateScale(size);
        auto translation = Matrix::CreateTranslation(matrix.Translation());

        auto DrawAxis = [&](Vector3 dir) {
            auto rotation = DirectionToRotationMatrix(dir);
            auto transform = rotation * scale * translation * Render::ViewProjection;
            Debug::DrawArrow(cmdList, transform, Colors::GlobalOrientation);
        };

        DrawAxis(matrix.Forward());
        DrawAxis(matrix.Up());
        DrawAxis(matrix.Right());
    }

    constexpr Color GetWallColor(const Wall& wall) {
        if (wall.Type == WallType::Door) {
            if ((ubyte)wall.Keys & (ubyte)WallKey::Blue) return Colors::DoorBlue;
            if ((ubyte)wall.Keys & (ubyte)WallKey::Gold) return Colors::DoorGold;
            if ((ubyte)wall.Keys & (ubyte)WallKey::Red) return Colors::DoorRed;
            return Colors::Door;
        }

        return Colors::Wall;
    }

    // draws markers for all walls and triggers
    void DrawWallMarkers(Level& level) {
        for (auto& wall : level.Walls) {
            if (!wall.IsValid()) continue;

            auto& seg = level.GetSegment(wall.Tag.Segment);
            //auto verts = VerticesForSide(*CurrentLevel, seg, wall.Side);
            auto face = Face::FromSide(level, seg, wall.Tag.Side);
            //auto& normal = seg.GetSide(wall.Side).Normals[0];
            auto center = face.Center() + face.AverageNormal() * Debug::WallMarkerOffset;

            // fade distant markers
            auto distance = Vector3::Distance(Camera.Position, center);
            auto alpha = std::clamp((500.0f - distance) / 500.0f, 0.1f, 0.65f);
            Color color = GetWallColor(wall);

            const auto trigger = level.TryGetTrigger(wall.Trigger);
            if (trigger) {
                color = Colors::Trigger;

                for (auto& target : trigger->Targets) {
                    if (!level.SegmentExists(target) || target.Side == SideID::None)
                        continue;

                    auto& targetSeg = level.GetSegment(target.Segment);
                    Vector3 targetCenter;
                    Color arrowColor;

                    bool isMatcenTrigger =
                        level.IsDescent1() ? trigger->HasFlag(TriggerFlagD1::Matcen) : trigger->Type == TriggerType::Matcen;

                    // Check that the target is actually a matcen (for D1)
                    if (isMatcenTrigger && targetSeg.Matcen != MatcenID::None) {
                        auto segVerts = targetSeg.GetVertices(level);
                        targetCenter = AverageVectors(segVerts);
                        arrowColor = Colors::Matcen;
                    }
                    else {
                        auto targetFace = Face::FromSide(level, targetSeg, target.Side);
                        targetCenter = targetFace.Center() + targetFace.AverageNormal() * Debug::WallMarkerOffset;
                        arrowColor = Colors::TriggerArrow;
                    }
                    Debug::DrawArrow(center, targetCenter, arrowColor);
                }
            }

            color.A(alpha);
            Debug::DrawWallMarker(face, color);

            if (wall.Type == WallType::Open)
                Debug::DrawArrow(center, center - face.AverageNormal() * 5, color);
        }
    }

    void DrawReactorTriggers(Level& level) {
        Object* reactor = nullptr;
        for (auto& obj : level.Objects) {
            if (obj.Type == ObjectType::Reactor || IsBossRobot(obj)) {
                reactor = &obj;
                break;
            }
        }

        if (reactor) {
            for (auto& target : level.ReactorTriggers) {
                auto& seg = level.GetSegment(target.Segment);
                auto targetFace = Face::FromSide(level, seg, target.Side);
                auto targetCenter = targetFace.Center() + targetFace.AverageNormal() * Debug::WallMarkerOffset;
                Debug::DrawArrow(reactor->Position, targetCenter, Colors::ReactorTriggerArrow);
            }
        }
    }

    void DrawMarkedFaces(Level& level) {
        for (auto& tag : Editor::Marked.Faces) {
            if (!level.SegmentExists(tag.Segment)) continue;
            auto [seg, side] = level.GetSegmentAndSide(tag);

            if (side.Wall == WallID::None) {
                Debug::DrawSideOutline(Game::Level, seg, tag.Side, Colors::MarkedFace);
                //if (seg.SideHasConnection(tag.Side)) continue; // skip connections for clarity
                Debug::DrawSide(Game::Level, seg, tag.Side, Colors::MarkedFaceFill);
            }
            else {
                Debug::DrawSideOutline(Game::Level, seg, tag.Side, Colors::MarkedWall);
                Debug::DrawSide(Game::Level, seg, tag.Side, Colors::MarkedWallFill);
                auto face = Face::FromSide(level, seg, tag.Side);
                Debug::DrawLine(face.Center(), face.Center() + face.AverageNormal() * 5, Colors::MarkedWall);
            }
        }
    }

    void DrawWireframe(Level& level) {
        bool hideMarks = Editor::Bindings::Active.IsBindingHeld(Editor::EditorAction::HideMarks);

        for (auto& seg : level.Segments) {
            Color color = Colors::Wireframe;
            color.w = Settings::Editor.WireframeOpacity;
            Color fill;

            if (seg.Type != SegmentType::None)
                std::tie(color, fill) = Colors::ForSegment(seg.Type);

            Debug::OutlineSegment(level, seg, color, &fill);
        }

        if (!hideMarks) {
            for (auto& wall : level.Walls) {
                auto color = GetWallColor(wall);
                color.A(0.12f);
                Debug::DrawSide(Game::Level, wall.Tag, color);
            }
        }
    }

    void DrawPath(span<const NavPoint> path, const Color& color) {
        if (path.size() < 2) return;
        Vector3 prev = path[0].Position;
        Debug::DrawPoint(prev, color);

        for (int i = 1; i < path.size(); i++) {
            Debug::DrawLine(prev, path[i].Position, color);
            Debug::DrawPoint(path[i].Position, color);
            prev = path[i].Position;
        }
    }

    void DrawMarked(ID3D12GraphicsCommandList* cmdList, Level& level) {
        bool hideMarks = Editor::Bindings::Active.IsBindingHeld(Editor::EditorAction::HideMarks);
        bool drawTranslationGizmo = true, drawRotationGizmo = true, drawScaleGizmo = true;

        switch (Settings::Editor.SelectionMode) {
            default:
            case Editor::SelectionMode::Face:
                if (!hideMarks)
                    DrawMarkedFaces(level);
                break;

            case Editor::SelectionMode::Segment:
                if (!hideMarks) {
                    for (auto& id : Editor::Marked.Segments) {
                        if (!level.SegmentExists(id)) continue;
                        auto& seg = level.GetSegment(id);

                        auto [outline, fill] = Colors::ForSegment(seg.Type);
                        for (auto& side : SIDE_IDS) {
                            Debug::DrawSideOutline(level, seg, side, outline);
                            if (seg.SideHasConnection(side)) continue; // skip fill for clarity
                            Debug::DrawSide(level, seg, side, fill);
                        }
                    }
                }
                break;

            case Editor::SelectionMode::Edge:
            case Editor::SelectionMode::Point:
                if (!hideMarks) {
                    for (auto& p : Editor::Marked.Points) {
                        if (!level.VertexIsValid(p)) continue;
                        auto& v = level.Vertices[p];
                        Debug::DrawPoint(v, Colors::MarkedPoint);
                    }
                }
                break;

            case Editor::SelectionMode::Object:
            {
                drawScaleGizmo = false;

                if (auto obj = level.TryGetObject(Editor::Selection.Object))
                    DrawObjectOutline(*obj, Colors::SelectedObject);
                else
                    drawTranslationGizmo = drawRotationGizmo = drawScaleGizmo = false;

                for (auto& id : Editor::Marked.Objects) {
                    if (auto obj = level.TryGetObject(id))
                        DrawObjectOutline(*obj, Colors::MarkedObject, 1.1f);
                }
                break;
            }
        }

        DrawUserCSysMarker(cmdList);
        if (Editor::Gizmo.State != Editor::GizmoState::Dragging) {
            if (drawTranslationGizmo) DrawTranslationGizmo(cmdList, Editor::Gizmo, Render::ViewProjection);
            if (drawRotationGizmo) DrawRotationGizmo(Editor::Gizmo);
            if (drawScaleGizmo) DrawScaleGizmo(cmdList, Editor::Gizmo, Render::ViewProjection);
        }
        else {
            DrawGizmoPreview(Editor::Gizmo);
        }
    }

    void DrawRooms(Level& level) {
        if (!Settings::Editor.ShowPortals) return;
        //std::array colors = { Color(1, 0.75, 0.5), Color(1, 0.5, 0.75), Color(0.75, 0.5, 1), Color(0.5, 0.75, 1), Color(0.5, 1, 0.75), Color(0.75, 1, 0.5) };
        //int colorIndex = 0;

        for (auto& room : level.Rooms) {
            //colorIndex = (colorIndex + 1) % 4;
            //for (auto& sid : room.Segments) {
            //    if (auto seg = level.TryGetSegment(sid)) {
            //        for (auto& side : SideIDs) {
            //            if (seg->SideHasConnection(side)) continue;
            //            Debug::DrawSideOutline(level, *seg, side, colors[colorIndex]);
            //        }
            //    }
            //}

            for (auto& portal : room.Portals) {
                if (auto seg = level.TryGetSegment(portal.Tag)) {
                    Debug::DrawSide(Game::Level, *seg, portal.Tag.Side, Colors::Portal);
                    auto side = Face::FromSide(level, *seg, portal.Tag.Side);
                    Debug::DrawArrow(side.Center(), side.Center() + side.AverageNormal() * 5, Color(0, 1, 0));
                }
            }
        }
    }


    void DrawRoomVisibility(Level& level, RoomID roomId) {
        auto room = level.GetRoom(roomId);
        if (!room) return;

        Color color(0.39f, 0.58f, .93f, 0.5f);

        for (auto& nid : room->NearbyRooms) {
            if (auto r = level.GetRoom(nid)) {
                for (auto& segId : r->Segments) {
                    if (auto seg = level.TryGetSegment(segId))
                        Debug::OutlineSegment(level, *seg, color);
                }
            }
        }

        /*for (auto& segId : room->VisibleSegments) {
            if (auto seg = level.TryGetSegment(segId))
                Debug::OutlineSegment(level, *seg, color);
        }*/
    }

    void DrawEditor(ID3D12GraphicsCommandList* cmdList, Level& level) {
        if (Settings::Editor.ShowWireframe)
            DrawWireframe(level);

        if (Settings::Editor.EnableWallMode) {
            DrawWallMarkers(level);
            DrawReactorTriggers(level);
        }

        if (Settings::Editor.ShowFlickeringLights) {
            for (auto& fl : level.FlickeringLights) {
                if (!level.SegmentExists(fl.Tag)) continue;
                auto face = Face::FromSide(level, fl.Tag);
                Debug::DrawFacingSquare(face.Center(), 4, Color(1, 1, 0, 0.5f));
            }
        }

        DrawMarked(cmdList, level);
        DrawSelection(Editor::Selection, level);

        //if (level.HasSecretExit()) {
        //    if (auto seg = level.TryGetSegment(level.SecretExitReturn)) {
        //        level.SecretReturnOrientation.Translation(seg->Center);
        //        DrawSecretLevelReturn(cmdList, level.SecretReturnOrientation);
        //    }
        //}

        if (Input::GetMouseMode() != Input::MouseMode::Normal)
            Debug::DrawCrosshair(Settings::Editor.CrosshairSize);

        if (Settings::Editor.ShowLevelTitle) {
            auto strSize = MeasureString(level.Name, FontSize::Big) * Shell::DpiScale;
            auto x = Editor::MainViewportXOffset + Editor::MainViewportWidth / 2 - strSize.x / 2;
            Render::Canvas->DrawGameText(level.Name, x, Editor::TopToolbarOffset, FontSize::Big, Color(1, 1, 1), 1 / Render::Canvas->GetScale());
        }
        //{
        //    auto tag = Editor::Selection.PointTag();
        //    if (level.SegmentExists(tag)) {
        //        //auto& seg = level.GetSegmentAndSide(Editor::Selection.Tag());
        //        auto& side = level.GetSide(tag);
        //        auto& normal = side.NormalForEdge(tag.Point);
        //        auto& pos = side.CenterForEdge(tag.Point);
        //        Debug::DrawLine(pos, pos + normal * 2.5, Colors::Door);
        //    }
        //}

        //DrawFaceNormals(level);
        if (Settings::Editor.Windows.TunnelBuilder)
            DrawTunnelBuilder(level);

        {
            Debug::DrawLine(Inferno::Debug::RayStart, Inferno::Debug::RayEnd, Color(1, 0, 0));
            Debug::DrawPoint(Inferno::Debug::RayStart, Color(1, 0, 0));
            Debug::DrawPoint(Inferno::Debug::RayEnd, Color(1, 0, 0));
        }
        DrawPath(Inferno::Debug::NavigationPath, Color(0.25, .5, 1));
        DrawRooms(level);
        OutlineBossTeleportSegments();

        if (Settings::Graphics.OutlineVisibleRooms) {
            if (auto seg = level.TryGetSegment(Editor::Selection.Segment))
                DrawRoomVisibility(level, seg->Room);
        }
    }

    void CreateEditorResources() {}

    void ReleaseEditorResources() {}
}
