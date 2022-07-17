#include "pch.h"
#include "Types.h"
#include "Level.h"
#include "Editor.h"
#include "Editor.Texture.h"
#include "Editor.Segment.h"
#include "Editor.Wall.h"
#include "Editor.IO.h"

namespace Inferno::Editor {
    void RotateUV(Vector2& uv, const Vector2& pivot, float angle) {
        uv -= pivot;
        auto radius = uv.Length();
        auto a = atan2(uv.y, uv.x) - angle;
        uv = { radius * cos(a), radius * sin(a) }; // convert back to rectangular coordinates
        uv += pivot;
    }

    void RotateTexture(SegmentSide& side, float angle) {
        for (auto& uv : side.UVs) {
            auto radius = uv.Length();
            auto a = atan2(uv.y, uv.x) - angle;
            uv = { radius * cos(a), radius * sin(a) }; // convert back to rectangular coordinates
        }
    }

    void TranslateTexture(SegmentSide& side, const Vector2& translation) {
        for (auto& uv : side.UVs)
            uv += translation;
    }

    void ScaleTexture(SegmentSide& side, Vector2 scale) {
        for (auto& uv : side.UVs)
            uv *= scale;
    }

    bool PointIsLeftOfLine(const Vector2& a, const Vector2& b, const Vector2& p) {
        return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) > 0;
    }

    float LineDistance(const Vector2& p1, const Vector2& p2, const Vector2& p0) {
        return ((p2.x - p1.x) * (p1.y - p0.y) - (p1.x - p0.x) * (p2.y - p1.y)) / (p2 - p1).Length();
    }

    // This doesn't work reliably because points at different distances need different scaling
    void ScaleTextureRelative(SegmentSide& side, const Vector2& scale, const Vector2& origin, const Vector2& xAxis) {
        Vector2 yAxis = { xAxis.y, -xAxis.x };

        auto xb = origin + xAxis;
        auto yb = origin + yAxis;

        for (auto& uv : side.UVs) {
            Vector2 uvnorm = uv;
            uvnorm.Normalize();
            //if (rightAxis.Cross(uv) > 0)

            //auto ySign = xAxis.Cross(uv).x > 0 ? 1 : -1;  // point above x axis
            auto yDist = LineDistance(origin, origin + xAxis, uv);
            auto xDist = LineDistance(origin, origin + yAxis, uv);
            auto ySign = PointIsLeftOfLine(origin, xb, uv) ? 1 : -1;  // point above x axis
            auto xSign = PointIsLeftOfLine(origin, yb, uv) ? 1 : -1;  // point to right of axis
            auto y = xAxis * (scale.y - 1.0f) * (float)ySign;
            auto x = xAxis * (scale.x - 1.0f) * (float)xSign;

            if (std::abs(yDist) > 0.01f)
                uv += y;

            if (std::abs(xDist) > 0.01f)
                uv += x;
        }
    }

    // Returns unscaled default UVs
    Array<Vector2, 3> GetTriangleUVs(Array<Vector3, 3> verts) {
        auto vec1 = verts[1] - verts[0];
        auto vec2 = verts[2] - verts[0];
        Vector3 proj = vec1;
        if (proj.Length() == 0) // degenerate face
            proj = Vector3::UnitY;

        proj.Normalize();
        auto projSF = proj.Dot(vec2);
        proj *= projSF;
        auto rej = vec2 - proj;
        return { Vector2::Zero, {0, vec1.Length()}, {-rej.Length(), projSF} };
    }

    void ResetUVs(Level& level, Tag tag, int edge, float extraAngle) {
        auto face = Face::FromSide(level, tag);

        Array<Vector2, 4> result;

        switch (face.Side.Type) {
            default:
            {
                // Split on 0,2
                auto C1 = GetTriangleUVs(face.VerticesForPoly0());
                auto C2 = GetTriangleUVs(face.VerticesForPoly1());

                // Rotate triangle C2 UVs onto C1 UVs so points 0 and 2 match (only need to calculate last point)
                float angle = -atan2(-C1[2].x, C1[2].y);
                RotateUV(C2[2], C2[0], angle);

                result = { C1[0], C1[1], C1[2], C2[2] };
            }
            break;

            case SideSplitType::Tri13:
            {
                // Split on 1,3
                auto C1 = GetTriangleUVs(face.VerticesForPoly0());
                auto C2 = GetTriangleUVs(face.VerticesForPoly1());

                // Rotate and translate triangle C2 UVs onto C1 UVs so points 1 and 3 match
                float angle = atan2(C1[1].x - C1[2].x, C1[1].y - C1[2].y);
                RotateUV(C2[2], C2[0], angle);
                C2[2] += C1[2];
                result = { C1[0], C1[1], C2[2], C1[2] };
            }
            break;
        }

        {
            // Align to selected edge
            auto translate = result[edge % 4] - result[0];
            for (auto& uv : result)
                uv -= translate; // shift edge to 0, 0

            auto v0 = result[1] - result[0]; // default edge alignment
            auto v1 = result[(1 + edge) % 4] - result[(0 + edge) % 4]; // selected edge

            v0.Normalize();
            v1.Normalize();
            auto angle = atan2(v1.y, v1.x) - atan2(v0.y, v0.x) + extraAngle;
            for (auto& uv : result)
                RotateUV(uv, result[edge], angle - DirectX::XM_PIDIV2);
        }

        // Scale UVs from world space to texture space
        for (int i = 0; i < 4; i++) {
            face.Side.UVs[i].x = result[i].x / 20.0f;
            face.Side.UVs[i].y = result[i].y / 20.0f;
        }
    }

    // Remaps UVs to their minimum values. i.e. u: 4-5 becomes u: 0-1
    void RemapUVs(SegmentSide& side) {
        Vector2 min = { FLT_MIN, FLT_MAX };
        Vector2 max = { FLT_MIN, FLT_MAX };

        for (int16 i = 0; i < 4; i++) {
            min.x = std::min(min.x, side.UVs[i].x);
            min.y = std::min(min.y, side.UVs[i].y);
            max.x = std::max(max.x, side.UVs[i].x);
            max.y = std::max(max.y, side.UVs[i].y);
        }

        Vector2 shift;
        if (min.x > 1) shift.x = -std::floor(min.x);
        if (min.y > 1) shift.y = -std::floor(min.y);

        if (max.x < -1) shift.x = -std::ceil(max.x);
        if (max.y < -1) shift.y = -std::ceil(max.y);

        for (auto& uv : side.UVs)
            uv += shift;
    }

    // Aligns texture from one face to another. Note that it does not propagate mirroring.
    bool Align(Level& level, Tag src, Tag dest, bool resetUvs) {
        auto edges = FindSharedEdges(level, src, dest);
        if (!edges) return false;
        auto& [srcEdge, destEdge] = *edges;
        auto& srcSide = level.GetSide(src);
        auto& destSide = level.GetSide(dest);

        if (resetUvs) {
            ResetUVs(level, dest);

            // Scale the dest to the source
            auto srcLen = (srcSide.UVs[(srcEdge + 1) % 4] - srcSide.UVs[srcEdge]).Length();
            auto destLen = (destSide.UVs[(destEdge + 1) % 4] - destSide.UVs[destEdge]).Length();
            auto scale = srcLen / destLen;
            ScaleTexture(destSide, { scale, scale });
        }

        // shift dest uvs to 0,0
        Vector2 uv0 = destSide.UVs[(destEdge + 1) % 4];
        for (int i = 0; i < 4; i++)
            destSide.UVs[i] -= uv0;

        auto uvRef = destSide.UVs[(destEdge + 1) % 4] - srcSide.UVs[srcEdge];
        // wrap to -1, 1
        uvRef.x = fmodf(uvRef.x, 1);
        uvRef.y = fmodf(uvRef.y, 1);

        // Find angle between the two edges
        auto srcAngle = [side = srcSide, edge = srcEdge] {
            return atan2(side.UVs[(edge + 1) % 4].y - side.UVs[edge].y,
                         side.UVs[(edge + 1) % 4].x - side.UVs[edge].x);
        }();

        // Dest goes in opposite direction
        auto destAngle = [side = destSide, edge = destEdge] {
            return atan2(side.UVs[edge].y - side.UVs[(edge + 1) % 4].y,
                         side.UVs[edge].x - side.UVs[(edge + 1) % 4].x);
        }();

        auto angle = destAngle - srcAngle;

        for (auto& uv : destSide.UVs) {
            auto radius = uv.Length();
            auto a = atan2(uv.y, uv.x) - angle; // rotate
            uv = { radius * cos(a), radius * sin(a) }; // convert back to rectangular coordinates
        }

        for (auto& uv : destSide.UVs)
            uv -= uvRef; // shift to source reference

        return true;
    }

    void AlignMarked(Level& level, Tag start, span<Tag> faces, bool resetUvs) {
        Set<Tag> visited; // only visit each face once
        Stack<Tag> search;
        search.push(start);

        List<Tag> marked;

        for (auto& mark : faces) {
            if (mark == start) continue;
            if (HasVisibleTexture(level, mark))
                marked.push_back(mark);
        }

        while (!search.empty()) {
            Tag src = search.top();
            search.pop();
            visited.insert(src);

            if (!level.SegmentExists(src)) continue;

            for (auto& mark : marked) {
                if (visited.contains(mark)) continue;

                if (Align(level, src, mark, resetUvs))
                    search.push(mark); // marked face touched this one, search it too
            }
        }
    }

    void OnSelectTexture(LevelTexID tmap1, LevelTexID tmap2) {
        for (auto& tag : GetSelectedFaces()) {
            if (!Game::Level.SegmentExists(tag)) continue;
            auto& side = Game::Level.GetSide(tag);
            auto wclip = WClipID::None;

            if (tmap2 != LevelTexID::None) {
                side.TMap2 = tmap2;
            }

            if (tmap1 != LevelTexID::None) {
                side.TMap = tmap1;
                wclip = Resources::GetWallClipID(tmap1);
            }

            if (side.TMap == side.TMap2)
                side.TMap2 = LevelTexID::Unset; // Unset if overlay is the same as tmap1

            if (side.TMap2 > LevelTexID::Unset)
                wclip = Resources::GetWallClipID(tmap2);

            if (wclip != WClipID::None)
                SetTextureFromWallClip(Game::Level, tag, wclip);
        }

        Events::LevelChanged();
        Editor::History.SnapshotLevel("Set texture");
    }

    // Checks if the current file is dirty. Returns true if the file can be closed.
    bool CanCloseCurrentFile() {
        if (!Editor::History.Dirty()) return true;

        auto file = Game::Mission ? Game::Mission->Path.filename().wstring() : Convert::ToWideString(Game::Level.FileName);
        auto msg = fmt::format(L"Do you want to save changes to {}?", file);
        auto result = ShowYesNoCancelMessage(msg.c_str(), L"File has changed");
        if (!result) return false; // Cancelled
        if (*result) Editor::Commands::Save(); // Yes
        return true;
    }

    Vector2 GetTranslationUV(float delta, const Vector2& uvTangent, const Vector2& uvBitangent, GizmoAxis axis) {
        Vector2 translation;
        if (axis == GizmoAxis::X)
            translation = uvBitangent * delta;
        else if (axis == GizmoAxis::Y)
            translation = -uvTangent * delta;

        return translation;
    }

    void TransformFaceUVs(Level& level,
                          Tag selection,
                          span<Tag> faces,
                          const TransformGizmo& gizmo,
                          const Vector2& uvTangent,
                          const Vector2& uvBitangent) {
        for (auto& tag : faces) {
            if (!level.SegmentExists(tag)) continue;
            auto& side = level.GetSide(tag);

            switch (gizmo.Mode) {
                case TransformMode::Translation:
                {
                    auto delta = gizmo.Delta / 20;
                    Vector2 translation = GetTranslationUV(delta, uvTangent, uvBitangent, gizmo.SelectedAxis);
                    for (auto& uv : side.UVs) uv += translation;
                    break;
                }
                case TransformMode::Rotation:
                    if (gizmo.SelectedAxis == GizmoAxis::Z) {
                        if (tag == selection) {
                            // do a nice rotation around the selected point if face is selected
                            Vector3 origin = Vector3(side.UVs[Editor::Selection.Point]);
                            auto transform = Matrix::CreateTranslation(-origin) * Matrix::CreateRotationZ(-gizmo.Delta) * Matrix::CreateTranslation(origin);
                            for (auto& uv : side.UVs)
                                uv = Vector2::Transform(uv, transform);
                        }
                        else {
                            // Otherwise just rotate locally
                            RotateTexture(side, gizmo.Delta);
                        }
                    }
                    break;
                case TransformMode::Scale:
                {
                    auto delta = gizmo.Delta / 20;
                    Vector2 scale = { 1, 1 };
                    if (gizmo.SelectedAxis == GizmoAxis::X) scale.x -= delta;
                    else if (gizmo.SelectedAxis == GizmoAxis::Y) scale.y -= delta;

                    if (tag == selection) {
                        auto origin = Vector3(side.UVs[Editor::Selection.Point]);
                        auto angle = atan2(uvBitangent.y, uvBitangent.x);
                        auto transform = Matrix::CreateTranslation(-origin) * Matrix::CreateRotationZ(-angle) * Matrix::CreateScale(Vector3(scale)) * Matrix::CreateRotationZ(angle) * Matrix::CreateTranslation(origin);
                        for (auto& uv : side.UVs)
                            uv = Vector2::Transform(uv, transform);
                    }
                    else {
                        ScaleTexture(side, scale);
                    }
                    break;
                }
            }
        }
    }


    // Same as tranform point except with two points
    void TransformEdgeUVs(Level& level, PointTag tag, const TransformGizmo& gizmo, const Vector2& uvTangent, const Vector2& uvBitangent) {
        auto& side = level.GetSide(tag);

        switch (gizmo.Mode) {
            case TransformMode::Translation:
                auto delta = gizmo.Delta / 20;
                Vector2 translation = GetTranslationUV(delta, uvTangent, uvBitangent, gizmo.SelectedAxis);
                side.UVs[tag.Point % 4] += translation;
                side.UVs[(tag.Point + 1) % 4] += translation;
                break;
        }
    }

    //void TranslatePointUV(float delta, const Vector2& uvTangent, const Vector2& uvBitangent) {
    //    Vector2 translation;
    //    if (gizmo.SelectedAxis == GizmoAxis::X)
    //        translation = bitangentUV * delta;
    //    else if (gizmo.SelectedAxis == GizmoAxis::Y)
    //        translation = -tangentUV * delta;

    //    face.Side.UV[Editor::Selection.Point] += translation;
    //}

    void TransformPointUV(Level& level, PointTag tag, const TransformGizmo& gizmo, const Vector2& uvTangent, const Vector2& uvBitangent) {
        auto& side = level.GetSide(tag);

        switch (gizmo.Mode) {
            case TransformMode::Translation:
                auto delta = gizmo.Delta / 20;
                Vector2 translation = GetTranslationUV(delta, uvTangent, uvBitangent, gizmo.SelectedAxis);
                side.UVs[tag.Point % 4] += translation;
                break;
        }
    }

    void OnTransformTextures(Level& level, const TransformGizmo& gizmo) {
        if (gizmo.Delta == 0) return;

        auto selection = Editor::Selection.PointTag();
        auto face = Face::FromSide(level, selection);
        auto uvTangent = face.VectorForEdgeUV(selection.Point);
        auto uvBitangent = Vector2(uvTangent.y, -uvTangent.x);

        switch (Settings::SelectionMode) {
            case SelectionMode::Face:
            case SelectionMode::Segment:
            {
                auto faces = GetSelectedFaces();
                TransformFaceUVs(level, selection, faces, gizmo, uvTangent, uvBitangent);
                break;
            }

            case SelectionMode::Edge:
            {
                TransformEdgeUVs(level, selection, gizmo, uvTangent, uvBitangent);
                break;
            }

            case SelectionMode::Point:
            {
                TransformPointUV(level, selection, gizmo, uvTangent, uvBitangent);
                break;
            }
        }

        if (gizmo.Mode != TransformMode::Scale && Settings::SelectionMode == SelectionMode::Face) {
            auto marked = Seq::ofSet(Editor::Marked.Faces);
            AlignMarked(Game::Level, Editor::Selection.Tag(), marked, false);
        }
        Events::LevelChanged();
    }

    string OnResetUVs() {
        for (auto& face : GetSelectedFaces())
            Editor::ResetUVs(Game::Level, face, Editor::Selection.Point, Settings::ResetUVsAngle * 90 * DegToRad);

        Events::LevelChanged();
        return "Reset UVs";
    }

    // Mirrors UVs along an axis
    void MirrorUVs(SegmentSide& side, const Vector2& p0, const Vector2& p1) {
        Vector2 u = p1 - p0;
        Vector2 n = { -u.y, u.x }; // line perpendicular to axis

        for (auto& uv : side.UVs) {
            // https://math.stackexchange.com/questions/108980/projecting-a-point-onto-a-vector-2d
            Vector2 v = uv - p1; // point relative to axis
            auto proj = n * n.Dot(v) / n.Dot(n); // project v onto n: b * (a . b) / ||b||^2
            uv -= proj * 2; // shift original point by twice the projected vector
        }
    }

    void Commands::FlipTextureV() {
        for (auto& tag : GetSelectedFaces()) {
            if (!Game::Level.SegmentExists(tag)) continue;
            // get vector from uvs of selected edge
            auto& side = Game::Level.GetSide(tag);
            auto& uv0 = side.UVs[Editor::Selection.Point];
            auto& uv1 = side.UVs[(Editor::Selection.Point + 1) % 4];
            MirrorUVs(side, uv0, uv1);
        }

        Events::LevelChanged();
        Editor::History.SnapshotLevel("Flip UVs");
    }

    void Commands::FlipTextureU() {
        for (auto& tag : GetSelectedFaces()) {
            if (!Game::Level.SegmentExists(tag)) continue;
            // get vector from uvs of selected edge
            auto& side = Game::Level.GetSide(tag);
            auto& uv0 = side.UVs[Editor::Selection.Point];
            auto& uv1 = side.UVs[(Editor::Selection.Point + 3) % 4];
            MirrorUVs(side, uv0, uv1);
        }

        Events::LevelChanged();
        Editor::History.SnapshotLevel("Flip UVs");
    }

    void Commands::RotateOverlay() {
        int rotation = Input::ShiftDown ? -1 : 1;
        for (auto& face : GetSelectedFaces()) {
            if (auto side = Game::Level.TryGetSide(face)) {
                side->OverlayRotation = (OverlayRotation)mod((uint16)side->OverlayRotation + rotation, 4);
            }
        }
        Events::LevelChanged();
        Editor::History.SnapshotLevel("Rotate Overlay");
    }

    bool CopyUVsToOtherSide(Level& level, Tag src) {
        if (!level.SegmentExists(src)) return false;
        auto [seg, side] = level.GetSegmentAndSide(src);
        if (!seg.SideHasConnection(src.Side)) {
            SetStatusMessage("Side does not have a connection");
            return false;
        }

        if (side.Wall == WallID::None) {
            SetStatusMessage("Side does not have a wall");
            return false;
        }

        auto srcIndices = seg.GetVertexIndices(src.Side);

        auto destTag = level.GetConnectedSide(src);
        if (!level.SegmentExists(destTag)) {
            ShowErrorMessage(L"Connected segment doesn't exist. This shouldn't happen.");
            return false;
        }

        auto [destSeg, destSide] = level.GetSegmentAndSide(destTag);
        auto destIndices = destSeg.GetVertexIndices(destTag.Side);

        // map the src and dest indices
        int mapping[4]{};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                if (srcIndices[i] == destIndices[j]) {
                    mapping[i] = j;
                    break;
                }
            }
        }

        // copy
        for (int i = 0; i < destSide.UVs.size(); i++)
            destSide.UVs[i] = side.UVs.at(mapping[i]);

        destSide.TMap = side.TMap;
        destSide.TMap2 = side.TMap2;
        Events::LevelChanged();
        return true;
    }

    void CopyUVsToFaces(Level& level, Tag src, span<Tag> faces) {
        if (!level.SegmentExists(src)) return;

        const auto& srcSide = level.GetSide(src);
        for (auto& face : faces) {
            if (!level.SegmentExists(face)) continue;
            level.GetSide(face).UVs = srcSide.UVs;
        }

        Events::LevelChanged();
    }

    string OnCopyUVs() {
        auto marked = Editor::Marked.GetMarkedFaces();
        if (marked.empty()) {
            if (!CopyUVsToOtherSide(Game::Level, Editor::Selection.Tag()))
                return {};
            return "Copy UVs to Other Side";
        }
        else {
            CopyUVsToFaces(Game::Level, Editor::Selection.Tag(), marked);
            return "Copy UVs to Faces";
        }
    }

    string OnAlignMarked() {
        // it'd be nice to tell the user that align marked needs the
        // selected face to touch or overlap the marked faces
        auto marked = Seq::ofSet(Editor::Marked.Faces);
        Editor::AlignMarked(Game::Level, Editor::Selection.Tag(), marked, Settings::ResetUVsOnAlign);
        Events::LevelChanged();
        return "Align Marked";
    }

    bool CubeMapping(Level& level, Tag src, span<Tag> faces, int edge) {
        if (!level.SegmentExists(src)) return false;
        auto alignmentFace = Face::FromSide(level, src);

        const auto& origin = alignmentFace[edge];

        Vector2 uvxAxis = alignmentFace.Side.UVs[(edge + 1) % 4] - alignmentFace.Side.UVs[edge % 4];
        auto xAxis = alignmentFace[(edge + 1) % 4] - alignmentFace[edge % 4];
        auto ratio = uvxAxis.Length() / std::max(xAxis.Length(), 0.001f);
        xAxis.Normalize();

        const auto zAxis = alignmentFace.AverageNormal();
        const auto yAxis = xAxis.Cross(zAxis);

        auto ProjectUV = [&](const Vector3& vert, const Vector3& normal) {
            auto shifted = vert - origin;

            std::array angles = {
                std::min(AngleBetweenVectors(normal, xAxis), AngleBetweenVectors(normal, -xAxis)),
                std::min(AngleBetweenVectors(normal, yAxis), AngleBetweenVectors(normal, -yAxis)),
                std::min(AngleBetweenVectors(normal, zAxis), AngleBetweenVectors(normal, -zAxis))
            };

            auto minIndex = std::distance(angles.begin(), ranges::min_element(angles));

            float x{}, y{};
            switch (minIndex) {
                case 0: // x axis
                    x = yAxis.Dot(shifted);
                    y = zAxis.Dot(shifted);
                    break;

                case 1: // y axis
                    x = xAxis.Dot(shifted);
                    y = zAxis.Dot(shifted);
                    break;

                case 2: // z axis
                    x = xAxis.Dot(shifted);
                    y = yAxis.Dot(shifted);
                    break;
            }

            return Vector2(x * ratio, y * ratio);
        };

        // project points and convert into UV coords, then wrap
        for (auto& id : faces) {
            if (!level.SegmentExists(id)) continue;
            auto face = Face::FromSide(level, id);

            for (int i = 0; i < 4; i++)
                face.Side.UVs[i] = ProjectUV(face[i], face.AverageNormal());

            RemapUVs(face.Side);
        }

        Events::LevelChanged();
        return true;
    }

    bool PlanarMapping(Level& level, Tag src, span<Tag> faces, int edge) {
        if (!level.SegmentExists(src)) return false;
        auto alignmentFace = Face::FromSide(level, src);

        const auto& origin = alignmentFace[edge];

        Vector2 uvxAxis = alignmentFace.Side.UVs[(edge + 1) % 4] - alignmentFace.Side.UVs[edge % 4];
        auto xAxis = alignmentFace[(edge + 1) % 4] - alignmentFace[edge % 4];
        auto ratio = uvxAxis.Length() / std::max(xAxis.Length(), 0.001f);
        xAxis.Normalize();
        auto yAxis = xAxis.Cross(alignmentFace.AverageNormal());

        auto ProjectUV = [&](const Vector3& vert) {
            auto shifted = vert - origin;
            auto x = xAxis.Dot(shifted); // project shifted point onto each axis
            auto y = yAxis.Dot(shifted);
            return Vector2(x * ratio, y * ratio);
        };

        // project points and convert into UV coords, then wrap
        for (auto& id : faces) {
            if (!level.SegmentExists(id)) continue;
            auto face = Face::FromSide(level, id);

            for (int i = 0; i < 4; i++)
                face.Side.UVs[i] = ProjectUV(face[i]);

            RemapUVs(face.Side);
        }

        Events::LevelChanged();
        return true;
    }

    string OnPlanarMapping() {
        auto faces = GetSelectedFaces();
        if (!PlanarMapping(Game::Level, Editor::Selection.Tag(), faces, Editor::Selection.Point))
            return {};

        return "Planar Mapping";
    }

    string OnCubeMapping() {
        auto faces = GetSelectedFaces();
        if (!CubeMapping(Game::Level, Editor::Selection.Tag(), faces, Editor::Selection.Point))
            return {};

        return "Cube Mapping";
    }

    namespace Commands {
        Command ResetUVs{ .SnapshotAction = OnResetUVs, .Name = "Reset UVs" };
        Command AlignMarked{ .SnapshotAction = OnAlignMarked, .Name = "Align Marked" };
        Command CopyUVsToFaces{ .SnapshotAction = OnCopyUVs, .Name = "Copy UVs to Faces" };
        Command PlanarMapping{ .SnapshotAction = OnPlanarMapping, .Name = "Planar Mapping" };
        Command CubeMapping{ .SnapshotAction = OnCubeMapping, .Name = "Cube Mapping" };
    }
}
