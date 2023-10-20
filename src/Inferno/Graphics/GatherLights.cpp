#include "pch.h"

#include "Game.Segment.h"
#include "Render.h"
#include "Resources.h"

namespace Inferno::Graphics {
    Vector4 ClipToView(const Vector4& clip, const Matrix& inverseProj) {
        Vector4 view = Vector4::Transform(clip, inverseProj);
        return view / view.w;
    }

    Vector4 ScreenToView(const Vector4& screen, const Matrix& inverseProj) {
        Vector2 texCoord(screen.x / Render::Camera.Viewport.width, screen.y / Render::Camera.Viewport.height);
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

    Option<Vector3> TriangleContainsUV(const Face2& face, int tri, Vector2 uv) {
        auto& indices = face.Side->GetRenderIndices();

        Vector2 uvs[3];
        for (int i = 0; i < 3; i++) {
            uvs[i] = face.Side->UVs[indices[tri * 3 + i]];
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

    Option<Vector3> FaceContainsUV(const Face2& face, Vector2 uv) {
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

        constexpr float eps = 0.001;
        if (t >= -eps && t <= 1 + eps && u >= -eps && u <= 1 + eps)
            return a + t * r; // intersects

        return {};
    }


    List<LightData> GatherSegmentLights(Level& level, const Segment& seg, float multiplier, float defaultRadius) {
        List<LightData> sources;
        TextureLightInfo defaultInfo{ .Radius = defaultRadius };

        if (seg.Type == SegmentType::Energy) {
            auto len = seg.GetLongestSide();

            auto energyId = level.IsDescent1() ? LevelTexID(328) : LevelTexID(353);
            auto mat = TryGetValue(Resources::LightInfoTable, energyId);
            auto color = mat ? mat->Color : Color(0.63f, 0.315f, 0.045f);

            LightData light{};
            light.color = color;
            light.radius = len * 2.0f;
            light.type = LightType::Point;
            light.pos = seg.Center;
            sources.push_back(light);
        }

        //if (HasFlag(seg.AmbientSound, SoundFlag::AmbientLava)) {
        {
            // todo: only do this on rooms containing lava
            auto lavaId = LevelTexID::None;

            Vector3 center;
            int sideCount = 0;
            float maxArea = 0;
            auto lavaSide = SideID::None;

            // there might be lava on a side, check them
            for (auto& sideId : SideIDs) {
                if (seg.SideHasConnection(sideId) && !seg.SideIsWall(sideId)) continue; // open sides can't have lights

                auto& side = seg.GetSide(sideId);
                auto tmap = LevelTexID::None;

                if (Resources::GetLevelTextureInfo(side.TMap).HasFlag(TextureFlag::Volatile)) {
                    tmap = side.TMap;
                    center += side.Center + side.AverageNormal; // center + offset
                    sideCount++;
                }

                if (Resources::GetLevelTextureInfo(side.TMap2).HasFlag(TextureFlag::Volatile)) {
                    tmap = side.TMap2;
                    center += side.Center + side.AverageNormal; // center + offset
                    sideCount++;
                }

                if (tmap != LevelTexID::None) {
                    // check if this side is the biggest
                    auto face = Face2::FromSide(level, seg, sideId);
                    auto v0 = face[1] - face[0];
                    auto v1 = face[3] - face[0];
                    auto area = v0.Cross(v1).Length();

                    if (area > maxArea) {
                        maxArea = area;
                        lavaSide = sideId;
                        lavaId = tmap;
                    }
                }
            }

            //if (lavaSide != SideID::None && lavaId != LevelTexID::None && sideCount > 0) {
            //    // Lava in this seg, add point lights
            //    center /= (float)sideCount;

            //    auto mat = TryGetValue(Resources::LightInfoTable, lavaId);
            //    auto color = mat ? mat->Color : Color(1.0f, 0.0f, 0.0f);

            //    auto face = Face2::FromSide(level, seg, lavaSide);
            //    auto iLongest = face.GetLongestEdge();
            //    auto longestVec = face[iLongest + 1] - face[iLongest];
            //    auto maxLen = longestVec.Length();
            //    auto iShortest = face.GetShortestEdge();
            //    auto shortestLen = (face[iShortest + 1] - face[iShortest]).Length();
            //    auto ratio = longestVec.Length() / std::max(0.1f, shortestLen);

            //    auto addLavaPoint = [&sources, &color](const Vector3& p, float radius) {
            //        LightData light{};
            //        light.color = color;
            //        light.radiusSq = radius * radius * 2.0f;
            //        light.type = LightType::Point;
            //        light.pos = p;
            //        sources.push_back(light);
            //    };

            //    maxLen = std::clamp(maxLen, 0.0f, 60.0f); // clamp for perf reasons

            //    constexpr float SPLIT_RATIO = 2.5f;
            //    if (ratio > SPLIT_RATIO && maxLen > 45) {
            //        addLavaPoint(center + longestVec / 4, maxLen * 0.5f);
            //        addLavaPoint(center - longestVec / 4, maxLen * 0.5f);
            //    }
            //    else {
            //        addLavaPoint(center, maxLen * 1.25f);
            //    }
            //}
        }

        for (auto& sideId : SideIDs) {
            if (seg.SideHasConnection(sideId) && !seg.SideIsWall(sideId)) continue; // open sides can't have lights
            if (auto wall = level.TryGetWall(seg.GetSide(sideId).Wall)) {
                if (wall->Type == WallType::Open) continue; // Skip open walls
            }

            auto face = Face2::FromSide(level, seg, sideId);
            auto& side = *face.Side;

            bool useOverlay = side.TMap2 > LevelTexID::Unset;

            TextureLightInfo* info = nullptr;

            // priority: mat2, tmap2, mat1, tmap1
            auto mat2 = TryGetValue(Resources::LightInfoTable, side.TMap2);
            if (mat2) {
                info = mat2;
            }
            else {
                auto& tmap2 = Resources::GetLevelTextureInfo(side.TMap2);
                if (tmap2.Lighting <= 0)
                    useOverlay = false;
            }

            if (!useOverlay) {
                auto mat = TryGetValue(Resources::LightInfoTable, side.TMap);
                if (mat)
                    info = mat;
            }

            if (!info) info = &defaultInfo;
            auto color = GetLightColor(side, true);
            // todo: need a separate property for dynamic light radius for perf reasons
            //auto radius = side.LightRadiusOverride ? side.LightRadiusOverride.value() * 3 : info->Radius;
            auto radius = info->Radius;

            if (!CheckMinLight(color)) continue;

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

            constexpr float SAMPLE_DIST = 0.1f;
            auto getUVScale = [&face](const Vector3& pos, Vector2 uv) {
                auto rightPos = FaceContainsUV(face, uv + Vector2(SAMPLE_DIST, 0));
                auto upPos = FaceContainsUV(face, uv + Vector2(0, SAMPLE_DIST));
                if (!rightPos || !upPos) return Vector2(20, 20);
                auto widthScale = (*rightPos - pos).Length() / SAMPLE_DIST;
                auto heightScale = (*upPos - pos).Length() / SAMPLE_DIST;
                return Vector2(widthScale, heightScale);
            };

            Vector2 centerUv = (side.UVs[0] + side.UVs[1] + side.UVs[2] + side.UVs[3]) / 4;
            auto uvScale = getUVScale(side.Center, centerUv);
            if (useOverlay && overlayAngle != 0) {
                constexpr Vector2 offset(0.5, 0.5);
                uvScale = RotateVector(uvScale - offset, -overlayAngle) + offset;
            }

            Vector2 prevIntersects[2];

            float offset = info->Offset;
            //if (side.Normals[0].Dot(side.Normals[1]) < 0.9f)
            //    offset += 2.0f; // Move lights of non-planar surfaces outward to prevent intersection with the wall

            // iterate each tile, checking the defined UVs
            for (int ix = xMin; ix < xMax; ix++) {
                for (int iy = yMin; iy < yMax; iy++) {
                    for (Vector2 lt : info->Points) {
                        LightData light{};
                        light.color = color * multiplier;
                        light.radius = radius;
                        light.normal = side.AverageNormal;
                        light.type = info->Type;

                        if (info->Wrap == LightWrapMode::U || info->Wrap == LightWrapMode::V) {
                            //if (info.IsContinuous()) {
                            // project the uv to the edge and create two points offset by the light radius
                            Vector2 uvOffset{ (float)ix, (float)iy };

                            auto uv0 = lt;
                            auto uv1 = lt;
                            if (info->Wrap == LightWrapMode::U)
                                uv1 += Vector2(1, 0);

                            if (info->Wrap == LightWrapMode::V)
                                uv1 += Vector2(0, 1);

                            if (useOverlay && overlayAngle != 0) {
                                constexpr Vector2 offset(0.5, 0.5);
                                uv0 = RotateVector(uv0 - offset, -overlayAngle) + offset;
                                uv1 = RotateVector(uv1 - offset, -overlayAngle) + offset;
                            }

                            uv0 += uvOffset;
                            uv1 += uvOffset;

                            // Extend the begin/end uvs so they should always cross
                            auto uvVec = uv1 - uv0;
                            uvVec.Normalize();
                            //uv0 += uvVec * Vector2((float)std::abs(xMin), (float)std::abs(yMin));
                            //uv1 += uvVec * Vector2((float)std::abs(xMax), (float)std::abs(yMax));

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
                                constexpr float uvIntOffset = 0.01;

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

                                    light.type = LightType::Rectangle;
                                    light.pos = center + side.AverageNormal * offset;
                                    light.right = rightVec * info->Width * uvScale.x;
                                    light.up = up;
                                    light.up -= upVec * 1; // offset the ends to prevent hotspots
                                    sources.push_back(light);
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
                                auto rightVec = *rightPos - *pos;
                                rightVec.Normalize();

                                auto upVec = side.AverageNormal.Cross(rightVec);
                                upVec.Normalize();

                                // Rotate the direction vectors to match the overlay
                                if (useOverlay && overlayAngle != 0) {
                                    auto rotation = Matrix::CreateFromAxisAngle(side.AverageNormal, -overlayAngle);
                                    upVec = Vector3::Transform(upVec, rotation);
                                    rightVec = Vector3::Transform(rightVec, rotation);
                                }

                                // sample points close to the uv to get up/right axis
                                light.pos = *pos + side.AverageNormal * offset;
                                light.right = rightVec * info->Width * uvScale.x;
                                light.up = -upVec * info->Height * uvScale.y; // reverse for some reason
                                sources.push_back(light);
                            }
                        }
                    }
                }
            }
        }

        return sources;
    }

    List<List<LightData>> GatherLightSources(Level& level, float multiplier, float defaultRadius) {
        List<List<LightData>> roomSources;

        for (auto& room : level.Rooms) {
            List<LightData> sources;
            for (auto& segId : room.Segments) {
                if (auto seg = level.TryGetSegment(segId)) {
                    auto segLights = GatherSegmentLights(level, *seg, multiplier, defaultRadius);
                    Seq::append(sources, segLights);
                }
            }

            roomSources.push_back(std::move(sources));
        }

        return roomSources;
    }
}
