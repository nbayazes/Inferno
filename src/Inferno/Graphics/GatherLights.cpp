#include "pch.h"
#include "Face.h"
#include "Game.Segment.h"
#include "Render.Level.h"
#include "Resources.h"

namespace Inferno::Graphics {
    Vector4 ClipToView(const Vector4& clip, const Matrix& inverseProj) {
        Vector4 view = Vector4::Transform(clip, inverseProj);
        return view / view.w;
    }

    Vector4 ScreenToView(const Vector4& screen, const Matrix& inverseProj, const Camera& camera) {
        auto size = camera.GetViewportSize();
        Vector2 texCoord(screen.x / size.x, screen.y / size.y);
        texCoord.y = 1 - texCoord.y; // flip y
        // Convert to clip space. * 2 - 1 transforms from -1, 1 to 0 1
        Vector4 clip(texCoord.x * 2.0f - 1.0f, texCoord.y * 2.0f - 1.0f, screen.z, screen.w);
        return ClipToView(clip, inverseProj);
    }

    constexpr bool CheckMinLight(const Color& color) {
        return color.x + color.y + color.z >= 0.001f;
    }

    float CrossTriangle(const Vector2& v1, const Vector2& v2, const Vector2& v3) {
        // Area of a triangle without the div 2
        return (v1.x - v2.x) * (v2.y - v3.y) + (v1.y - v2.y) * (v3.x - v2.x);
        // (v1[0] - v2[0]) * (v2[1] - v3[1]) + (v1[1] - v2[1]) * (v3[0] - v2[0]);
        // (v1[0] - v2[0]) * (v2[1] - v3[1]) + (v1[1] - v2[1]) * (v3[0] - v2[0])
    }

    Vector3 BarycentricWeights(const Vector2& v1, const Vector2& v2, const Vector2& v3, const Vector2& point) {
        Vector3 w;
        w.x = CrossTriangle(v2, v3, point);
        w.y = CrossTriangle(v3, v1, point);
        w.z = CrossTriangle(v1, v2, point);
        auto wtot = w.x + w.y + w.z;
        if (wtot == 0)
            return { 1.0f / 3, 1.0f / 3, 1.0f / 3 }; // dummy values for zero area face

        return w / wtot; // normalize weights
    }

    // Interpolates a triangle value based on barycentric weights
    Vector3 InterpolateBarycentric(const Vector3& v1, const Vector3& v2, const Vector3& v3, const Vector3& w) {
        Vector3 p;
        p.x = v1.x + w.x + v2.x * w.y + v3.x * w.z;
        p.y = v1.y * w.x + v2.y * w.y + v3.y * w.z;
        p.z = v1.z * w.x + v2.z * w.y + v3.z * w.z;
        return p;
    }

    // transform_point_by_tri_v3
    Vector3 BarycentricTransform(const Vector3& point,
                                 const Vector3& srcP0,
                                 const Vector3& srcP1,
                                 const Vector3& srcP2,
                                 const Vector3& tarP0,
                                 const Vector3& tarP1,
                                 const Vector3& tarP2) {
        auto srcNorm = (srcP1 - srcP0).Cross(srcP2 - srcP0);
        srcNorm.Normalize();

        auto tarNorm = (tarP1 - tarP0).Cross(tarP2 - tarP0);
        tarNorm.Normalize();

        //normal_tri_v3(no_tar, tri_tar_p1, tri_tar_p2, tri_tar_p3);
        //normal_tri_v3(no_src, tri_src_p1, tri_src_p2, tri_src_p3);

        //axis_dominant_v3_to_m3(mat_src, no_src);

        //r_n1[0] = n[1] * d;
        //r_n1[1] = -n[0] * d;
        //r_n1[2] = 0.0f;
        //r_n2[0] = -n[2] * r_n1[1];
        //r_n2[1] = n[2] * r_n1[0];
        //r_n2[2] = n[0] * r_n1[1] - n[1] * r_n1[0];

        // Create transform to unproject the source
        const float d = 1.0f / sqrtf(srcNorm.LengthSquared());
        Vector3 r0(srcNorm.y * d, -srcNorm.x * d, 0);
        Vector3 r1(-srcNorm.z * r0.x, srcNorm.z * r0.x, srcNorm.x * r0.x - srcNorm.y * r0.y);
        Matrix m(r0, r1, srcNorm);

        // Project source triangle to 2D
        auto pointProj = Vector2(Vector3::Transform(point, m));
        auto tri0Proj = Vector2(Vector3::Transform(srcP0, m));
        auto tri1Proj = Vector2(Vector3::Transform(srcP1, m));
        auto tri2Proj = Vector2(Vector3::Transform(srcP2, m));

        /* make the source tri xy space */
        /*mul_v3_m3v3(pt_src_xy, mat_src, pt_src);
        mul_v3_m3v3(tri_xy_src[0], mat_src, tri_src_p1);
        mul_v3_m3v3(tri_xy_src[1], mat_src, tri_src_p2);
        mul_v3_m3v3(tri_xy_src[2], mat_src, tri_src_p3);*/

        auto weights = BarycentricWeights(tri0Proj, tri1Proj, tri2Proj, pointProj);
        //auto pt = InterpolateBarycentric(tarP0, tarP1, tarP2, weights);
        //barycentric_weights_v2(tri_xy_src[0], tri_xy_src[1], tri_xy_src[2], pt_src_xy, w_src);
        //interp_v3_v3v3v3(pt_tar, tri_tar_p1, tri_tar_p2, tri_tar_p3, w_src);

        //area_tar = sqrtf(area_tri_v3(tri_tar_p1, tri_tar_p2, tri_tar_p3));
        //area_src = sqrtf(area_tri_v2(tri_xy_src[0], tri_xy_src[1], tri_xy_src[2]));

        //z_ofs_src = pt_src_xy[2] - tri_xy_src[0][2];
        //madd_v3_v3v3fl(pt_tar, pt_tar, no_tar, (z_ofs_src / area_src) * area_tar);
        return weights;
    }

    //Vector3 PositionAtUV(const Face& face, int tri, Vector2 uv) {
    //    auto& indices = face.Side.GetRenderIndices();

    //    Vector2 uvs[3];
    //    for (int i = 0; i < 3; i++) {
    //        uvs[i] = face.Side.UVs[indices[tri * 3 + i]];
    //    }

    //    // https://math.stackexchange.com/a/28552
    //    // Vectors of two edges
    //    auto vec0 = uvs[1] - uvs[0];
    //    auto vec1 = uvs[2] - uvs[0];
    //    auto vecPt = uv - uvs[0];

    //    auto normal = vec0.Cross(vec1);

    //    // solve barycentric weights
    //    auto g = (vecPt.Cross(vec0) / -normal).x;
    //    auto f = (vecPt.Cross(vec1) / normal).x;

    //    // project UV to world using barycentric weights
    //    auto& v0 = face[indices[tri * 3 + 0]];
    //    auto& v1 = face[indices[tri * 3 + 1]];
    //    auto& v2 = face[indices[tri * 3 + 2]];
    //    return Vector3::Barycentric(v0, v1, v2, f, g);
    //}

    Option<Vector3> TriangleContainsUV(const ConstFace& face, int tri, Vector2 uv) {
        auto& indices = face.Side.GetRenderIndices();

        Vector2 uvs[3];
        for (int i = 0; i < 3; i++) {
            uvs[i] = face.Side.UVs[indices[tri * 3 + i]];
        }

        // https://math.stackexchange.com/a/28552
        // Vectors of two edges
        auto vec0 = uvs[1] - uvs[0];
        auto vec1 = uvs[2] - uvs[0];
        auto vecPt = uv - uvs[0];

        auto normal = vec0.Cross(vec1);

        // solve barycentric weights
        auto g = (vecPt.Cross(vec0) / -normal).x;
        auto f = (vecPt.Cross(vec1) / normal).x;

        if (g < 0 || f < 0 || g + f > 1)
            return {}; // point was outside of triangle

        // project UV to world using barycentric weights
        auto& v0 = face[indices[tri * 3 + 0]];
        auto& v1 = face[indices[tri * 3 + 1]];
        auto& v2 = face[indices[tri * 3 + 2]];
        return Vector3::Barycentric(v0, v1, v2, f, g);
    }

    Option<Vector3> FaceContainsUV(const ConstFace& face, Vector2 uv) {
        auto pos = TriangleContainsUV(face, 0, uv);
        if (!pos) pos = TriangleContainsUV(face, 1, uv);
        return pos;
    }

    Option<Vector2> IntersectLines(Vector2 a, Vector2 b, Vector2 c, Vector2 d) {
        auto r = b - a;
        auto s = d - c;
        auto den = r.Cross(s).x;
        if (den == 0) return {};

        auto u = ((c - a).Cross(r) / den).x;
        auto t = ((c - a).Cross(s) / den).x;

        constexpr float eps = 0.001f;
        if (t >= -eps && t <= 1 + eps && u >= -eps && u <= 1 + eps)
            return a + t * r; // intersects

        return {};
    }

    TextureLightInfo* GetSideTextureInfo(const SegmentSide& side) {
        bool useOverlay = side.TMap2 > LevelTexID::Unset;

        // Prioritize overlay texture lights
        if (useOverlay) {
            if (auto mat2 = Resources::GetLightInfo(side.TMap2))
                return mat2;
            else {
                if (!Resources::GetTextureInfo(side.TMap2).Transparent)
                    return nullptr; // Skip walls that have a solid texture over a light
            }
        }

        // Try base texture
        return Resources::GetLightInfo(side.TMap);
    }

    void GatherSideLights(const ConstFace& face, TextureLightInfo& info, List<LightData>& sources) {
        auto& side = face.Side;
        bool useOverlay = side.TMap2 > LevelTexID::Unset;

        // todo: need a separate property for dynamic light radius for perf reasons
        //auto radius = side.LightRadiusOverride ? side.LightRadiusOverride.value() * 3 : info->Radius;

        Vector2 minUV(FLT_MAX, FLT_MAX), maxUV(-FLT_MAX, -FLT_MAX);
        for (int j = 0; j < 4; j++) {
            auto& uv = side.UVs[j];
            minUV = Vector2::Min(minUV, uv);
            maxUV = Vector2::Max(maxUV, uv);
        }

        auto xMin = (int)std::round(minUV.x);
        auto yMin = (int)std::round(minUV.y);
        auto xMax = (int)std::round(maxUV.x);
        auto yMax = (int)std::round(maxUV.y);
        auto isFarFromEdge = [](float f) {
            auto diff = std::abs(f - std::round(f));
            return diff > 0.01f;
        };
        if (isFarFromEdge(minUV.x)) xMin -= 1;
        if (isFarFromEdge(minUV.y)) yMin -= 1;
        if (isFarFromEdge(maxUV.x)) xMax += 1;
        if (isFarFromEdge(maxUV.y)) yMax += 1;

        float overlayAngle = useOverlay ? GetOverlayRotationAngle(side.OverlayRotation) : 0;

        constexpr float SAMPLE_DIST = 0.01f;
        auto getUVScale = [&face](const Vector3& pos, Vector2 uv) {
            auto rightPos = FaceContainsUV(face, uv + Vector2(SAMPLE_DIST, 0));
            auto upPos = FaceContainsUV(face, uv + Vector2(0, SAMPLE_DIST));
            if (!rightPos || !upPos) return Vector2(20, 20);
            auto widthScale = (*rightPos - pos).Length() / SAMPLE_DIST;
            auto heightScale = (*upPos - pos).Length() / SAMPLE_DIST;
            return Vector2(widthScale, heightScale);
        };

        Vector2 uvScale(20, 20);

        auto verts = face.CopyPoints();
        auto& indices = side.GetRenderIndices();
        auto& v0 = verts[indices[0]];
        auto& v1 = verts[indices[1]];
        auto& v2 = verts[indices[2]];
        //auto& v3 = verts[indices[3]];

        //auto n0 = v2 - v0;
        //auto n1 = v3 - v1;
        //n0.Normalize();
        //n1.Normalize();
        //auto trueNormal = n1.Cross(n0);
        //auto trueCenter = side.Type == SideSplitType::Tri02 ? (v2 + v0) / 2 : (v3 + v1) / 2;

        Vector2 size = {
            Vector3::Distance(face.GetEdgeMidpoint(0), face.GetEdgeMidpoint(2)),
            Vector3::Distance(face.GetEdgeMidpoint(1), face.GetEdgeMidpoint(3))
        };

        //float trueOffset = 0;
        bool isPlanar = side.Normals[0].Dot(side.Normals[1]) > 0.99f;

        {
            auto& uv0 = side.UVs[indices[0]];
            auto& uv1 = side.UVs[indices[1]];
            auto& uv2 = side.UVs[indices[2]];
            auto uvcenter = (uv0 + uv1 + uv2) / 3;
            auto vcenter = (v0 + v1 + v2) / 3;
            uvScale = getUVScale(vcenter, uvcenter);

            // This was supposed to calculate the distance of the light position to a plane and offset it
            //if (!isPlanar) {
            //    if (side.Type == SideSplitType::Tri02) {
            //        n0 = v1 - v0;
            //        n1 = v3 - v0;
            //        n0.Normalize();
            //        n1.Normalize();
            //        trueOffset = DistanceFromPlane(side.Center, v0, n1.Cross(n0));
            //    }
            //    else {
            //        n0 = v0 - v1;
            //        n1 = v2 - v1;
            //        n0.Normalize();
            //        n1.Normalize();
            //        trueOffset = DistanceFromPlane(side.Center, v0, n1.Cross(n0));
            //    }
            //}
        }

        if (useOverlay && overlayAngle != 0) {
            constexpr Vector2 offset(0.5, 0.5);
            uvScale = RotateVector(uvScale - offset, -overlayAngle) + offset;
        }

        Vector2 prevIntersects[2];

        float offset = info.Offset;
        auto lightMode = side.LightMode;

        struct SurfaceLight {
            Vector2 UV;
            LightData Data;
            bool Visited = false;
        };

        List<SurfaceLight> sideSources;

        // iterate each tile, checking the defined UVs
        for (int ix = xMin; ix < xMax; ix++) {
            for (int iy = yMin; iy < yMax; iy++) {
                for (Vector2 lt : info.Points) {
                    LightData light{};
                    light.radius = info.Radius;
                    light.normal = side.AverageNormal;
                    light.type = info.Type;
                    light.coneAngle0 = info.Angle0;
                    light.coneAngle1 = info.Angle1;
                    light.coneSpill = info.ConeSpill;

                    if (info.Wrap == LightWrapMode::U || info.Wrap == LightWrapMode::V) {
                        //if (info.IsContinuous()) {
                        // project the uv to the edge and create two points offset by the light radius
                        Vector2 uvOffset{ (float)ix, (float)iy };

                        auto uv0 = lt;
                        auto uv1 = lt;
                        if (info.Wrap == LightWrapMode::U)
                            uv1 += Vector2(1, 0);

                        if (info.Wrap == LightWrapMode::V)
                            uv1 += Vector2(0, 1);

                        if (useOverlay && overlayAngle != 0) {
                            constexpr Vector2 origin(0.5, 0.5);
                            uv0 = RotateVector(uv0 - origin, -overlayAngle) + origin;
                            uv1 = RotateVector(uv1 - origin, -overlayAngle) + origin;
                        }

                        uv0 += uvOffset;
                        uv1 += uvOffset;

                        // Extend the begin/end uvs so they should always cross
                        auto uvVec = uv1 - uv0;
                        uvVec.Normalize();
                        uv0 -= uvVec * Vector2(100, 100);
                        uv1 += uvVec * Vector2(100, 100);

                        int found = 0;
                        Vector2 intersects[2];

                        // there should always be two intersections
                        for (int i = 0; i < 4; i++) {
                            if (auto intersect = IntersectLines(uv0, uv1, side.UVs[i], side.UVs[(i + 1) % 4])) {
                                intersects[found++] = *intersect;
                                if (found > 1) break;
                            }
                        }

                        if (found == 2) {
                            // Check if the previous intersections are on top of this one
                            if ((intersects[0] - prevIntersects[0]).Length() < 0.1 &&
                                (intersects[1] - prevIntersects[1]).Length() < 0.1)
                                continue; // Skip overlap

                            prevIntersects[0] = intersects[0];
                            prevIntersects[1] = intersects[1];

                            auto uvIntVec = intersects[1] - intersects[0];
                            uvIntVec.Normalize();
                            constexpr float uvIntOffset = 0.01f;

                            auto uvEdge0 = intersects[0] + uvIntVec * uvIntOffset;
                            auto uvEdge1 = intersects[1] - uvIntVec * uvIntOffset;
                            auto pos = FaceContainsUV(face, uvEdge0);
                            auto pos2 = FaceContainsUV(face, uvEdge1);

                            if (pos && pos2) {
                                // 'up' is the wrapped axis
                                auto delta = *pos2 - *pos;
                                auto up = delta / 2;
                                auto center = (*pos2 + *pos) / 2;
                                Vector3 upVec;
                                up.Normalize(upVec);
                                auto rightVec = side.AverageNormal.Cross(upVec);

                                auto uv = (uvEdge0 + uvEdge1) / 2;
                                light.type = LightType::Rectangle;
                                light.pos = center + side.AverageNormal * offset;
                                light.right = rightVec * info.Width * uvScale.x;
                                light.up = up;
                                light.up -= upVec * 0.5f; // offset the ends to prevent hotspots on adjacent walls
                                light.mode = lightMode;
                                sideSources.push_back({ uv, light });
                            }
                        }
                    }
                    else {
                        if (useOverlay && overlayAngle != 0) {
                            constexpr Vector2 origin(0.5, 0.5);
                            lt = RotateVector(lt - origin, -overlayAngle) + origin;
                        }

                        Vector2 uv = { ix + lt.x, iy + lt.y };

                        // Check both faces
                        auto pos = FaceContainsUV(face, uv);

                        // Sample points near the light position to determine UV scale
                        auto rightPos = FaceContainsUV(face, uv + Vector2(SAMPLE_DIST, 0));

                        if (pos && rightPos) {
                            if (!isPlanar) {
                                if (info.Type == LightType::Point) {
                                    // use the triangle the point is on as normal
                                    if (TriangleContainsUV(face, 0, uv))
                                        light.normal = side.Normals[0];
                                    else if (TriangleContainsUV(face, 1, uv))
                                        light.normal = side.Normals[1];
                                }
                                else if (info.Type == LightType::Rectangle) {
                                    // If the face size is small, assume the light is crossing it.
                                    if (size.x < 30 && size.y < 30)
                                        // todo: calculate exact distance instead of hard coding it
                                        offset += -2.0f;
                                        //offset += trueOffset;
                                        //    light.normal = trueNormal;
                                }
                            }

                            auto rightVec = *rightPos - *pos;
                            rightVec.Normalize();

                            auto upVec = light.normal.Cross(rightVec);
                            upVec.Normalize();

                            // Rotate the direction vectors to match the overlay
                            if (useOverlay && overlayAngle != 0) {
                                auto rotation = Matrix::CreateFromAxisAngle(light.normal, -overlayAngle);
                                upVec = Vector3::Transform(upVec, rotation);
                                rightVec = Vector3::Transform(rightVec, rotation);
                            }

                            // sample points close to the uv to get up/right axis
                            light.pos = *pos + light.normal * offset;
                            light.right = rightVec * info.Width * uvScale.x;
                            light.up = -upVec * info.Height * uvScale.y; // reverse for some reason
                            light.mode = lightMode;
                            sideSources.push_back({ uv, light });
                        }
                    }
                }
            }
        }

        if (sideSources.empty()) return;

        constexpr float MERGE_THRESHOLD = 0.0125f;

        List<SurfaceLight> buffer;

        // Deduplicate
        for (int i = 0; i < sideSources.size(); i++) {
            auto& light = sideSources[i];
            ASSERT(light.Data.normal != Vector3::Zero);
            if (light.Visited) continue;
            light.Visited = true;

            for (int j = 0; j < sideSources.size(); j++) {
                auto& other = sideSources[j];
                if (other.Visited) continue;

                if (std::abs(light.UV.x - other.UV.x) < MERGE_THRESHOLD && std::abs(light.UV.y - other.UV.y) < MERGE_THRESHOLD) {
                    other.Visited = true;
                }
            }

            buffer.push_back(light);
        }

        sideSources.clear();
        Seq::append(sideSources, buffer);
        for (auto& src : sideSources) {
            src.Visited = false;
        }

        int mergeMode = -1;
        if (info.Wrap == LightWrapMode::U) {
            mergeMode = side.OverlayRotation == OverlayRotation::Rotate0 || side.OverlayRotation == OverlayRotation::Rotate180;
        }
        else if (info.Wrap == LightWrapMode::V) {
            mergeMode = side.OverlayRotation == OverlayRotation::Rotate90 || side.OverlayRotation == OverlayRotation::Rotate270;
        }

        if (mergeMode != -1) {
            buffer.clear();

            // Merge nearby
            for (int i = 0; i < sideSources.size(); i++) {
                auto& light = sideSources[i];
                if (light.Visited) continue;
                light.Visited = true;

                for (int j = 0; j < sideSources.size(); j++) {
                    auto& other = sideSources[j];
                    if (other.Visited) continue;

                    bool merge = false;
                    if (mergeMode == 0 && std::abs(light.UV.x - other.UV.x) < 0.125f) {
                        merge = true;
                    }
                    else if (mergeMode == 1 && std::abs(light.UV.y - other.UV.y) < 0.125f) {
                        merge = true;
                    }

                    // Merge nearby lights
                    if (merge) {
                        other.Visited = true;
                        light.Data.pos = (light.Data.pos + other.Data.pos) / 2;
                        light.Data.right *= 2;
                    }
                }

                buffer.push_back(light);
            }
        }

        for (auto& light : buffer) {
            light.Data.normal.Normalize();
            sources.push_back(light.Data);
        }
    }

    List<SegmentLight> GatherSegmentLights(Level& level) {
        List<SegmentLight> segSources;

        for (int i = 0; i < level.Segments.size(); i++) {
            auto& seg = level.Segments[i];
            SegmentLight segLights;

            for (auto& sideId : SIDE_IDS) {
                if (seg.SideHasConnection(sideId) && !seg.SideIsWall(sideId)) continue; // open sides can't have lights
                if (auto wall = level.TryGetWall(seg.GetSide(sideId).Wall)) {
                    if (wall->Type == WallType::Open) continue; // Skip open walls
                }

                auto face = ConstFace::FromSide(level, seg, sideId);
                auto color = GetLightColor(face.Side, true);
                auto info = GetSideTextureInfo(face.Side);

                if (!CheckMinLight(color) || !info) continue;

                auto& sideLighting = segLights.Sides[(int)sideId];
                sideLighting.Color = color;
                //sideLighting.Color.Premultiply();
                //sideLighting.Color.w = 1;
                sideLighting.Radius = info->Radius;
                sideLighting.Tag = { SegID(i), sideId };

                GatherSideLights(face, *info, sideLighting.Lights);
            }

            if (seg.Type == SegmentType::Energy) {
                auto len = seg.GetLongestSide();

                auto energyId = level.IsDescent1() ? LevelTexID(328) : LevelTexID(353);
                auto mat = Resources::GetLightInfo(energyId);
                auto color = mat ? mat->Color : Color(0.63f, 0.315f, 0.045f);

                LightData light{};
                light.color = color;
                light.radius = len * 2.0f;
                light.radius = std::min(60.0f, light.radius); // Prevent excessively large lights
                light.type = LightType::Point;
                light.pos = seg.Center;
                segLights.Lights.push_back(light);
            }

            segSources.push_back(segLights);
        }

        return segSources;
    }
}
