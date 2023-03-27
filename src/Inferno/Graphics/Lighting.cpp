#include "pch.h"
#include "Lighting.h"
#include "Render.h"
#include "Game.h"

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

    // Returns the light contribution from both textures on this side
    Color GetLightColor(const SegmentSide& side) {
        if (side.LightOverride) return *side.LightOverride;

        auto& tmap1 = Resources::GetLevelTextureInfo(side.TMap);
        auto light = tmap1.Lighting;

        Color color;
        if (tmap1.Lighting > 0) {
            auto& ti = Resources::GetTextureInfo(side.TMap);
            color += ti.AverageColor;
        }

        if (side.HasOverlay()) {
            auto& tmap2 = Resources::GetLevelTextureInfo(side.TMap2);
            light += tmap2.Lighting;
            if (tmap2.Lighting > 0) {
                auto& ti = Resources::GetTextureInfo(side.TMap2);
                color += ti.AverageColor;
            }
        }

        color.w = 0;
        return color * light;
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
        auto pt = InterpolateBarycentric(tarP0, tarP1, tarP2, weights);
        //barycentric_weights_v2(tri_xy_src[0], tri_xy_src[1], tri_xy_src[2], pt_src_xy, w_src);
        //interp_v3_v3v3v3(pt_tar, tri_tar_p1, tri_tar_p2, tri_tar_p3, w_src);

        //area_tar = sqrtf(area_tri_v3(tri_tar_p1, tri_tar_p2, tri_tar_p3));
        //area_src = sqrtf(area_tri_v2(tri_xy_src[0], tri_xy_src[1], tri_xy_src[2]));

        //z_ofs_src = pt_src_xy[2] - tri_xy_src[0][2];
        //madd_v3_v3v3fl(pt_tar, pt_tar, no_tar, (z_ofs_src / area_src) * area_tar);
        return weights;
    }

    Option<Vector3> TriangleContainsUV(const Face& face, int tri, Vector2 uv) {
        auto indices = face.Side.GetRenderIndices();

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

    Option<Vector3> FaceContainsUV(const Face& face, Vector2 uv) {
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

    List<LightData> GatherLightSources(Level& level, float multiplier = 1, float defaultRadius = 20) {
        List<LightData> sources;

        TextureLightInfo defaultInfo{ .Radius = defaultRadius };
        // The empty light texture is only used for ambient lighting

        for (int segIdx = 0; segIdx < level.Segments.size(); segIdx++) {
            auto& seg = level.Segments[segIdx];

            for (auto& sideId : SideIDs) {
                if (seg.SideHasConnection(sideId) && !seg.SideIsWall(sideId)) continue; // open sides can't have lights
                auto face = Face::FromSide(level, seg, sideId);
                auto& side = face.Side;

                bool useOverlay = side.TMap2 > LevelTexID::Unset;

                TextureLightInfo* info = nullptr;

                // priority: mat2, tmap2, mat1, tmap1
                auto mat2 = TryGetValue(Resources::MaterialInfo.LevelTextures, side.TMap2);
                if (mat2) {
                    info = mat2;
                }
                else {
                    auto& tmap2 = Resources::GetLevelTextureInfo(side.TMap2);
                    if (tmap2.Lighting <= 0)
                        useOverlay = false;
                }

                if (!useOverlay) {
                    auto mat = TryGetValue(Resources::MaterialInfo.LevelTextures, side.TMap);
                    if (mat)
                        info = mat;
                }

                if (!info) info = &defaultInfo;
                auto color = info->Color == Color(0, 0, 0) ? GetLightColor(side) : info->Color;
                auto radius = side.LightRadiusOverride ? side.LightRadiusOverride.value() * 3 : info->Radius;

                if (side.LightOverride) color = *side.LightOverride;
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

                Vector2 prevIntersects[2];

                // iterate each tile, checking the defined UVs
                for (int ix = xMin; ix < xMax; ix++) {
                    for (int iy = yMin; iy < yMax; iy++) {
                        for (Vector2 lt : info->Points) {
                            LightData light{};
                            light.color = color.ToVector3() * multiplier;
                            light.radiusSq = radius * radius;
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

                                uv0 -= uvVec * Vector2(10, 10);
                                uv1 += uvVec * Vector2(10, 10);

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

                                    auto pos = FaceContainsUV(face, intersects[0] + uvIntVec * uvIntOffset);
                                    auto pos2 = FaceContainsUV(face, intersects[1] - uvIntVec * uvIntOffset);

                                    if (pos && pos2) {
                                        // 'up' is the wrapped axis
                                        auto up = (*pos2 - *pos) / 2;
                                        auto center = (*pos2 + *pos) / 2;
                                        Vector3 upVec;
                                        up.Normalize(upVec);
                                        auto rightVec = side.AverageNormal.Cross(upVec);

                                        light.type = LightType::Rectangle;
                                        light.pos = center + side.AverageNormal * info->Offset;
                                        light.right = rightVec * info->Width;
                                        light.up = up;
                                        light.up -= upVec * 1; // offset the ends to prevent hotspots
                                        sources.push_back(light);
                                    }
                                }
                            }
                            else {
                                if (useOverlay && overlayAngle != 0) {
                                    constexpr Vector2 offset(0.5, 0.5);
                                    lt = RotateVector(lt - offset, -overlayAngle) + offset;
                                }

                                Vector2 uv = { ix + lt.x, iy + lt.y };

                                // Check both faces
                                auto pos = FaceContainsUV(face, uv);
                                auto rightPos = FaceContainsUV(face, uv + Vector2(0.1, 0));

                                if (pos && rightPos) {
                                    // todo: scale right / up as a UV on the face
                                    auto rightVec = *rightPos - *pos;
                                    rightVec.Normalize();
                                    auto upVec = side.AverageNormal.Cross(rightVec);
                                    //auto upVec = rightVec.Cross(side.AverageNormal);
                                    upVec.Normalize();

                                    // sample points close to the uv to get up/right axis
                                    light.pos = *pos + side.AverageNormal * info->Offset;
                                    light.right = rightVec * info->Width;
                                    light.up = -upVec * info->Height; // reverse for some reason
                                    sources.push_back(light);
                                }
                            }
                        }
                    }
                }
            }
        }

        return sources;
    }

    void FillLightGridCS::SetLightConstants(uint32 width, uint32 height) {
        LightingConstants psConstants{};
        //psConstants.sunDirection = m_SunDirection;
        //psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
        //psConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
        //psConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
        psConstants.InvTileDim[0] = 1.0f / LIGHT_GRID;
        psConstants.InvTileDim[1] = 1.0f / LIGHT_GRID;
        psConstants.TileCount[0] = AlignedCeil(width, (uint32)LIGHT_GRID);
        psConstants.TileCount[1] = AlignedCeil(height, (uint32)LIGHT_GRID);
        //psConstants.FirstLightIndex[0] = Lighting::m_FirstConeLight;
        //psConstants.FirstLightIndex[1] = Lighting::m_FirstConeShadowedLight;
        psConstants.FrameIndexMod2 = Render::Adapter->GetCurrentFrameIndex();

        _lightingConstantsBuffer.Begin();
        _lightingConstantsBuffer.Copy({ &psConstants, 1 });
        _lightingConstantsBuffer.End();
    }

    std::array<LightData, MAX_LIGHTS> LIGHT_BUFFER{};

    void FillLightGridCS::SetLights(ID3D12GraphicsCommandList* cmdList) {
        auto sources = GatherLightSources(Game::Level, 1, 60);

        for (int i = 0; i < MAX_LIGHTS; i++) {
            if (i < sources.size()) {
                auto& source = sources[i];
                LIGHT_BUFFER[i] = source;
            }
            else {
                LIGHT_BUFFER[i].radiusSq = 0;
            }
        }

        _lightUploadBuffer.Begin();
        for (auto& light : LIGHT_BUFFER) {
            _lightUploadBuffer.Copy(light);
        }
        _lightUploadBuffer.End();

        _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyResource(_lightData.Get(), _lightUploadBuffer.Get());
    }

    void FillLightGridCS::Dispatch(ID3D12GraphicsCommandList* cmdList, ColorBuffer& linearDepth) {
        //ScopedTimer _prof(L"FillLightGrid", gfxContext);
        PIXBeginEvent(cmdList, PIX_COLOR_DEFAULT, L"Fill Light Grid");

        //ColorBuffer& LinearDepth = g_LinearDepth[TemporalEffects::GetFrameIndexMod2()];

        //auto depthState = depth.Transition(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto linearDepthState = linearDepth.Transition(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        //color.Transition(cmdList, 

        _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        _lightGrid.Transition(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        _bitMask.Transition(cmdList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        uint32_t tileCountX = AlignedCeil((int)_width, LIGHT_GRID);
        //uint32_t tileCountY = AlignedCeil((int)color.GetHeight(), LIGHT_GRID);

        float farClip = Inferno::Render::Camera.FarClip;
        float nearClip = Inferno::Render::Camera.NearClip;
        const float rcpZMagic = nearClip / (farClip - nearClip);

        CSConstants constants{};
        constants.ViewportWidth = _width;
        constants.ViewportHeight = _height;
        constants.InvTileDim = 1.0f / LIGHT_GRID;
        constants.RcpZMagic = rcpZMagic;
        constants.TileCount = tileCountX;

        auto& camera = Inferno::Render::Camera;
        //constants.ViewProjMatrix = camera.ViewProj();
        constants.ViewMatrix = camera.View;
        //constants.ViewProjMatrix = camera.Projection * camera.View;
        constants.InverseProjection = camera.Projection.Invert();

        //Vector3 v0 = { -0.730262637, -0.414881557, 0.500083327 };
        //Vector3 v2 = { -0.730262637, -0.410143405, 0.500083327 };
        //auto proj = camera.Projection;
        //auto view = camera.View;
        //auto projView = camera.Projection * camera.View;
        //auto viewProj = camera.ViewProj();

        //Matrix lhView = DirectX::XMMatrixPerspectiveFovLH(0.785398185, 1.35799503, 1, 1000);
        //Matrix proj2 = GetProjMatrixTest(0.785398185, 1.35799503, 1, 1000);
        //Matrix proj2 = GetProjMatrixTest(Settings::Editor.FieldOfView * DegToRad, camera.Viewport.AspectRatio(), camera.NearClip, camera.FarClip);
        //Matrix rhView = DirectX::XMMatrixPerspectiveFovRH(0.785398185, 1.35799503, 1, 1000);
        // lhview2 aspect width is 3.278 instead of 1.7777

        //Matrix view2 = DirectX::XMMatrixLookAtLH(camera.Position, camera.Target, camera.Up);
        //Matrix view2 = SetLookDirection(camera.Target - camera.Position, camera.Up);

        //auto viewProj2 = proj2 * view2;
        //auto viewProj3 = view2 * proj2;

        //DirectX::XMMATRIX mat{};
        //mat.r[0] = { -0.0351921320, -0.468572199, 0.882723868, 0.00000000 };
        //mat.r[1] = { 0.00000000, 0.883271098, 0.468862653, 0.00000000 };
        //mat.r[2] = { -0.999380529, 0.0165002793, -0.0310841799, 0.00000000 };
        //mat.r[3] = { -0.000122070312, -59.9769287, -1276.53467, 1.00000000 };

        //DirectX::XMMATRIX projMat{};
        //projMat.r[0] = { 1.35799503, 0.00000000, 0.00000000, 0.00000000 };
        //projMat.r[1] = { 0.00000000, 2.41421342, 0.00000000, 0.00000000 };
        //projMat.r[2] = { 0.00000000, 0.00000000, 0.000100010002, -1.00000000 };
        //projMat.r[3] = { 0.00000000, 0.00000000, 1.00010002, 0.00000000 };

        //auto xmViewProj = DirectX::XMMatrixMultiply(mat, projMat);
        //Matrix xmViewProj_(xmViewProj);

        //Matrix mat2{
        //    -0.0351921320, -0.468572199, 0.882723868, 0.00000000,
        //    0.00000000, 0.883271098, 0.468862653, 0.00000000,
        //    -0.999380529, 0.0165002793, -0.0310841799, 0.00000000,
        //    -0.000122070312, -59.9769287, -1276.53467, 1.00000000
        //};

        //Matrix projMat2{
        //    1.35799503, 0.00000000, 0.00000000, 0.00000000,
        //    0.00000000, 2.41421342, 0.00000000, 0.00000000,
        //    0.00000000, 0.00000000, 0.000100010002, -1.00000000,
        //    0.00000000, 0.00000000, 1.00010002, 0.00000000
        //};

        //auto xmProjMat3 = mat2 * projMat2;

        // for look at
        // this matches
        /*Vector3 testForward(-0.859759986, -0.509800017, 0.0302756485);
            Vector3 testUp(-0.509484231, 0.860292912, 0.0179410148);
            Matrix testLookAt = SetLookDirection(testForward, testUp);*/

        // hard code
        //constants.ViewportWidth = 1920;
        //constants.ViewportHeight = 1080;
        //constants.InvTileDim = 0.0625000000; // 1 / 16
        //constants.RcpZMagic = 0.000100010002;
        //constants.TileCount = 120;

        //constants.ViewProjMatrix.m[0][0] = -0.0477907397;
        //constants.ViewProjMatrix.m[0][1] = -1.13123333;
        //constants.ViewProjMatrix.m[0][2] = 8.82812164e-05;
        //constants.ViewProjMatrix.m[0][3] = -0.882723868;

        //constants.ViewProjMatrix.m[1][0] = 0.00000000;
        //constants.ViewProjMatrix.m[1][1] = 2.13240504;
        //constants.ViewProjMatrix.m[1][2] = 4.68909566e-05;
        //constants.ViewProjMatrix.m[1][3] = -0.468862653;

        //constants.ViewProjMatrix.m[2][0] = -1.35715377;
        //constants.ViewProjMatrix.m[2][1] = 0.0398351960;
        //constants.ViewProjMatrix.m[2][2] = -3.10872883e-06;
        //constants.ViewProjMatrix.m[2][3] = 0.0310841799;

        //constants.ViewProjMatrix.m[3][0] = -0.000165770878;
        //constants.ViewProjMatrix.m[3][1] = -144.797104;
        //constants.ViewProjMatrix.m[3][2] = 0.872433782;
        //constants.ViewProjMatrix.m[3][3] = 1276.53467;

        //constants.ViewProjMatrix = viewProj3;

        //Compute(constants);
        //Compute2(constants, { 0, 0 });

        _csConstants.Begin();
        _csConstants.Copy({ &constants, 1 });
        _csConstants.End();

        cmdList->SetComputeRootSignature(_rootSignature.Get());
        //cmdList->SetComputeRoot32BitConstants(B0_Constants, 28, &constants, 0);
        cmdList->SetComputeRootConstantBufferView(B0_Constants, _csConstants.GetGPUVirtualAddress());
        cmdList->SetComputeRootDescriptorTable(T0_LightBuffer, _lightData.GetSRV());
        cmdList->SetComputeRootDescriptorTable(T1_LinearDepth, linearDepth.GetSRV());
        cmdList->SetComputeRootDescriptorTable(U0_Grid, _lightGrid.GetUAV());
        cmdList->SetComputeRootDescriptorTable(U1_GridMask, _bitMask.GetUAV());
        cmdList->SetPipelineState(_pso.Get());

        //Context.Dispatch(tileCountX, tileCountY, 1);
        Dispatch2D(cmdList, _width, _height);

        _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        _lightGrid.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        _bitMask.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        //depth.Transition(cmdList, depthState);
        linearDepth.Transition(cmdList, linearDepthState);
        PIXEndEvent(cmdList);
    }
}
