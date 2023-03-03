#include "pch.h"
#include "Lighting.h"
#include "Render.h"
#include "Game.h"

namespace Inferno::Graphics {
    namespace {
        struct LightSource {
            Vector3 Position;
            Color Color;
            float Radius = 20;
        };
    }


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

    Option<Vector3> TriangleContainsUV(const Face& face, int tri, Vector2 uv, float overlayAngle) {
        auto indices = face.Side.GetRenderIndices();

        Vector2 uvs[3]{};
        for (int i = 0; i < 3; i++) {
            uvs[i] = face.Side.UVs[indices[tri * 3 + i]];
            //if (overlayAngle != 0) uvs[i] = RotateVector(uvs[i], -overlayAngle);
        }

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

    struct TextureLightInfo {
        List<Vector2> UVs = { { 0.5f, 0.5f } }; // UV positions for each light
        float Offset = 3.5; // light surface offset
        float Radius = 60; // light radius
        Color Color = { 1, 1, 1 };
    };

    // 0.25, 0.75, 1.25 - continuous spacing of two
    // 0.166, 0.5, 0.833 - spacing of three
    const List<Vector2> LeftJustifiedUVs = { { 0.125f, 1.0f / 6 }, { 0.125f, 3.0f / 6 }, { 0.125f, 5.0f / 6 } };

    Dictionary<LevelTexID, TextureLightInfo> TextureInfoD1 = {
        { LevelTexID(212), { .UVs = { { 0.25, 0.75 }, { 0.75, 0.75 } } } },
        { LevelTexID(250), { .UVs = LeftJustifiedUVs, .Offset = 1, .Radius = 30 } },
        { LevelTexID(251), { .UVs = LeftJustifiedUVs, .Offset = 1, .Radius = 30 } },
        { LevelTexID(286), { .UVs = { { 0.25f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 0.25f }, { 0.75f, 0.75f } }, .Offset = 3, .Radius = 30, .Color = Color(0.3, 0.3, 0.3) }, },
    };

    List<LightSource> GatherLightSources(Level& level, float multiplier = 1, float defaultRadius = 20) {
        List<LightSource> sources;

        TextureLightInfo defaultInfo{ .Radius = defaultRadius };

        for (int segIdx = 0; segIdx < level.Segments.size(); segIdx++) {
            auto& seg = level.Segments[segIdx];

            for (auto& sideId : SideIDs) {
                if (seg.SideHasConnection(sideId) && !seg.SideIsWall(sideId)) continue; // open sides can't have lights
                auto face = Face::FromSide(level, seg, sideId);
                auto& side = face.Side;
                auto color = GetLightColor(side);
                if (!CheckMinLight(color)) continue;

                // use the longest edge as X axis
                auto edge = face.GetLongestEdge();

                Vector2 uvX = side.UVs[(edge + 1) % 4] - side.UVs[edge];
                Vector2 uvY = side.UVs[(edge + 3) % 4] - side.UVs[edge];
                Vector2 uvVecX, uvVecY;
                uvX.Normalize(uvVecX);
                uvY.Normalize(uvVecY);

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
                auto isFar = [](float f) {
                    auto diff = std::abs(f - std::round(f));
                    return diff > 0.01f;
                };
                if (isFar(minUV.x)) xMin -= 1;
                if (isFar(minUV.y)) yMin -= 1;
                if (isFar(maxUV.x)) xMax += 1;
                if (isFar(maxUV.y)) yMax += 1;

                bool useOverlay = side.TMap2 > LevelTexID::Unset;
                float overlayAngle = GetOverlayRotationAngle(side.OverlayRotation);
                auto tmap = useOverlay ? side.TMap2 : side.TMap;

                auto& info = TextureInfoD1.contains(tmap) ? TextureInfoD1[tmap] : defaultInfo;

                // iterate each tile, checking the defined UVs
                for (int ix = xMin; ix < xMax; ix++) {
                    for (int iy = yMin; iy < yMax; iy++) {
                        for (auto lt : info.UVs) {
                            if (overlayAngle != 0) {
                                constexpr Vector2 offset(0.5, 0.5);
                                lt = RotateVector(lt - offset, -overlayAngle) + offset;
                            }

                            // todo: special case when uv is aligned to whole number and index equals min/max range
                            Vector2 uv = { ix + lt.x, iy + lt.y };

                            // Check both faces
                            auto pos = TriangleContainsUV(face, 0, uv, overlayAngle);
                            if (!pos) pos = TriangleContainsUV(face, 1, uv, overlayAngle);

                            if (pos) {
                                *pos += face.AverageNormal() * info.Offset;

                                LightSource light = {
                                    //.Indices = seg.GetVertexIndices(sideId),
                                    .Position = *pos,
                                    .Color = color * multiplier,
                                    .Radius = side.LightRadiusOverride.value_or(info.Radius),
                                };
                                sources.push_back(light);
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

    void FillLightGridCS::SetLights(ID3D12GraphicsCommandList* cmdList) {
        std::array<LightData, MAX_LIGHTS> lights{};
        auto sources = GatherLightSources(Game::Level, 0.18f, 60);

        for (int i = 0; i < MAX_LIGHTS && i < sources.size(); i++) {
            auto& source = sources[i];
            lights[i].pos = { source.Position.x, source.Position.y, source.Position.z };
            lights[i].color = { source.Color.R(), source.Color.G(), source.Color.B() };
            lights[i].radiusSq = source.Radius * source.Radius;
        }

        _lightUploadBuffer.Begin();
        for (auto& light : lights) {
            _lightUploadBuffer.Copy(light);
        }
        _lightUploadBuffer.End();

        _lightData.Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyResource(_lightData.Get(), _lightUploadBuffer.Get());
    }

    bool FillLightGridCS::Compute(const CSConstants& c) {
        constexpr Vector2 group{ 4, 4 };

        float tileMinDepth = 0.01f;
        float tileMaxDepth = 0.05f;
        float tileDepthRange = std::max(tileMaxDepth - tileMinDepth, FLT_MIN);
        float invTileDepthRange = 1 / tileDepthRange;
        Vector2 invTileSize2X = Vector2(c.ViewportWidth, c.ViewportHeight) * c.InvTileDim;

        Vector3 tileBias = {
            -2.0f * float(group.x) + invTileSize2X.x - 1.0f,
            -2.0f * float(group.y) + invTileSize2X.y - 1.0f,
            -tileMinDepth * invTileDepthRange
        };

        Matrix projToTile = {
            invTileSize2X.x, 0, 0, tileBias.x,
            0, -invTileSize2X.y, 0, tileBias.y,
            0, 0, invTileDepthRange, tileBias.z,
            0, 0, 0, 1
        };

        Matrix tileMVP = projToTile * c.ViewMatrix;

        Vector4 frustumPlanes[6];
        Vector4 tilePos(tileMVP.m[3]);
        auto pos = tileMVP.Translation(); // maybe?
        frustumPlanes[0] = tilePos + Vector4(tileMVP.m[0]);
        frustumPlanes[1] = tilePos - Vector4(tileMVP.m[0]);
        frustumPlanes[2] = tilePos + Vector4(tileMVP.m[1]);
        frustumPlanes[3] = tilePos - Vector4(tileMVP.m[1]);
        frustumPlanes[4] = tilePos + Vector4(tileMVP.m[2]);
        frustumPlanes[5] = tilePos - Vector4(tileMVP.m[2]);
        // normalize
        for (int n = 0; n < 6; n++)
            frustumPlanes[n] *= 1 / std::sqrt(Vector3(frustumPlanes[n]).Dot(Vector3(frustumPlanes[n])));

        LightData lightData{};
        lightData.pos = { 0, 0, 40 };
        lightData.color = { 1, 0, 0 };
        lightData.radiusSq = 30 * 30;

        //auto lightWorldPos = lightData.pos;
        float lightCullRadius = sqrt(lightData.radiusSq);

        bool overlapping = true;
        for (int p = 0; p < 6; p++) {
            Vector3 planeNormal(frustumPlanes[p]);
            float planeDist = frustumPlanes[p].w;
            float d = Vector3(lightData.pos.data()).Dot(planeNormal) + planeDist;
            //float d = dot(lightWorldPos, frustumPlanes[p].xyz) + frustumPlanes[p].w;
            if (d < -lightCullRadius) {
                overlapping = false;
            }
        }

        //if (!overlapping)
        //continue;

        return overlapping;
    }

    bool FillLightGridCS::Compute2(const CSConstants& c, Vector2 threadId) {
        Vector4 screenSpace[4]{};
        constexpr int BLOCK_SIZE = 8; // 8
        //threadId = Vector2{ std::floorf(c.ViewportWidth / 2 / BLOCK_SIZE), std::floorf(c.ViewportHeight / 2 / BLOCK_SIZE) };
        threadId = Vector2{ 152, 88 };
        constexpr float z = -1.0f;
        constexpr float w = 1.0f;
        // Top left
        screenSpace[0] = Vector4(threadId.x * BLOCK_SIZE, threadId.y * BLOCK_SIZE, z, w);
        // Top right
        screenSpace[1] = Vector4((threadId.x + 1) * BLOCK_SIZE, threadId.y * BLOCK_SIZE, z, w);
        // Bottom left
        screenSpace[2] = Vector4(threadId.x * BLOCK_SIZE, (threadId.y + 1) * BLOCK_SIZE, z, w);
        // Bottom right
        screenSpace[3] = Vector4((threadId.x + 1) * BLOCK_SIZE, (threadId.y + 1) * BLOCK_SIZE, z, w);

        Vector3 viewSpace[4];

        for (int i = 0; i < 4; i++) {
            viewSpace[i] = Vector3(ScreenToView(screenSpace[i], c.InverseProjection));
        }

        const Vector3 eyePos = Vector3::Zero;
        Plane planes[4];
        planes[0] = Plane(eyePos, viewSpace[0], viewSpace[2]); // Left
        planes[1] = Plane(eyePos, viewSpace[3], viewSpace[1]); // Right
        planes[2] = Plane(eyePos, viewSpace[1], viewSpace[0]); // Top
        planes[3] = Plane(eyePos, viewSpace[2], viewSpace[3]); // Bottom

        LightData light{};
        light.pos = { -25, 2, 70 };
        light.color = { 1, 0, 0 };
        light.radiusSq = 30 * 30;

        float radius = std::sqrt(light.radiusSq);
        bool inside = true;
        // project light pos into view space
        auto lightPos = Vector3::Transform(Vector3(light.pos.data()), c.ViewMatrix);

        DirectX::BoundingFrustum frustum(Render::Camera.Projection);
        bool contains = frustum.Contains(Vector3(light.pos.data()));

        for (int i = 0; i < 4; i++) {
            auto& plane = planes[i];
            auto dist = plane.Normal().Dot(lightPos) - plane.D();
            auto dist2 = plane.DotCoordinate(lightPos);
            auto dist3 = plane.DotNormal(lightPos);

            if (dist < -radius)
                //if (dist < 0)
                inside = false; // too far
        }

        Vector3 normal(0.9, 0, 0.25);
        normal.Normalize();
        Plane p(normal, 0);
        Vector3 lp(0, 2, 35);
        float dist4 = p.DotNormal(lp);

        Debug::InsideFrustum = inside;
        Debug::LightPosition = lightPos;
        return inside;
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

        Vector3 v0 = { -0.730262637, -0.414881557, 0.500083327 };
        Vector3 v2 = { -0.730262637, -0.410143405, 0.500083327 };
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
        Compute2(constants, { 0, 0 });

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
