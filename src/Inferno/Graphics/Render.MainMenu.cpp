#include "pch.h"
#include "Render.MainMenu.h"
#include "CameraContext.h"
#include "Graphics.h"
#include "Icosphere.h"
#include "OpenSimplex2.h"
#include "Render.h"
#include "Utility.h"

namespace Inferno {
    using namespace Inferno::Render;

    namespace {
        List<uint> AsteroidMeshIds;
        int IcosphereId = -1;
        int SunId = 1;
    }

    // Position camera with sun at left edge
    //Vector3 MenuCameraPosition = { -300, 0, 350 };
    //Vector3 MenuCameraTarget = { -300, 0, 0 };

    // Offset sun
    Vector3 MenuCameraPosition = { -105, 0, 175 };
    Vector3 MenuCameraTarget = { -105, 0, 0 };

    //Vector3 MenuCameraPosition = { 0, 0, 800 };
    //Vector3 MenuCameraTarget = { 0, 0, 0 };


    void ApplyNoise(span<ObjectVertex> vertices, const Vector3& scale, float noiseScale, int64 seed) {
        for (auto& v : vertices) {
            //auto s = 50.0f; // scale
            //Vector3 strength = { 1.0f, 1.0f, 1.0f };
            //strength *= 15;
            //auto seed = 0;

            auto& p = v.Position;
            //p += v.Normal * OpenSimplex2::Noise3(seed, p.x * s, p.y * s, p.z * s) * 20;
            auto ps = p / noiseScale;
            // multiply and clamp to make the shapes more jagged
            //auto x = std::clamp(OpenSimplex2::Noise3(seed, 0, ps.y, ps.z) * 2, -1.0f, 1.0f) * scale.x;
            //auto y = std::clamp(OpenSimplex2::Noise3(seed, ps.x, 0, ps.z) * 2, -1.0f, 1.0f) * scale.y;
            //auto z = std::clamp(OpenSimplex2::Noise3(seed, ps.x, ps.y, 0) * 2, -1.0f, 1.0f) * scale.z;
            auto x = Saturate(OpenSimplex2::Noise3(seed, 0, ps.y, ps.z) * 2) * scale.x * 1.5f;
            auto y = Saturate(OpenSimplex2::Noise3(seed, ps.x, 0, ps.z) * 2) * scale.y * 1.5f;
            auto z = Saturate(OpenSimplex2::Noise3(seed, ps.x, ps.y, 0) * 2) * scale.z * 1.5f;
            p += Vector3{ x, y, z };

            //s = 1.0f;
            //v.Position += v.Normal * OpenSimplex2::Noise3(0, v.Position.x * s, v.Position.y * s, v.Position.z * s) * 5;
            v.Color = Color(1, 1, 1);
        }
    }


    void GenerateAsteroids() {
        float radius = 25.0f;
        //float halfR = radius / 2;
        auto icosphere = CreateIcosphere(radius, 4);
        IcosphereId = GlobalMeshes->AddMesh(icosphere);
        auto sun = CreateIcosphere(1, 4);
        SunId = GlobalMeshes->AddMesh(sun);

        for (int i = 0; i < 10; i++) {
            ModelMesh copy = icosphere;

            //auto scaleMatrix = Matrix::CreateScale(25);
            auto scaleMatrix = Matrix::CreateScale(radius + PcgRandomFloat(i) * radius, radius + PcgRandomFloat(i + 1) * radius, radius + PcgRandomFloat(i + 2) * radius);
            for (auto& v : copy.Vertices) {
                v.Position = Vector3::Transform(v.Position, scaleMatrix);
            }

            ApplyNoise(copy.Vertices, { 6, 6, 6 }, radius * 4, i - 1);
            ApplyNoise(copy.Vertices, { 6, 6, 6 }, radius * 4, i - 100);
            ApplyNoise(copy.Vertices, { 6, 6, 6 }, radius * 4, i - 1000);
            ApplyNoise(copy.Vertices, { 3, 3, 3 }, radius * 3, i);
            //ApplyNoise(copy.Vertices, { 3, 3, 3 }, radius * 2, i + 1000);
            ApplyNoise(copy.Vertices, { 1, 1, 1 }, radius * 1, i + 10000);
            //ApplyNoise(copy.Vertices, { 1, 1, 1 }, 10, i + 100);

            auto scaleMatrix2 = Matrix::CreateScale(0.5f);
            for (auto& v : copy.Vertices) {
                v.Position = Vector3::Transform(v.Position, scaleMatrix2);
            }

            AsteroidMeshIds.push_back(GlobalMeshes->AddMesh(copy));
        }
    }

    void DrawAsteroid(const GraphicsContext& ctx, const Vector3& offset, const Vector3& rotation, float radians, float scale, int index) {
        if (GlobalMeshes->Meshes.empty()) return;

        auto cmdList = ctx.GetCommandList();

        AsteroidShader::Constants constants{};
        constants.Ambient = Color(0.5f, 0.5f, 0.5f);
        constants.World =
            Matrix::CreateScale(scale)
            * Matrix::CreateFromYawPitchRoll(rotation)
            * Matrix::CreateTranslation(offset)
            * Matrix::CreateRotationY(radians);
        Shaders->Asteroid.SetConstants(cmdList, constants);

        //auto index = RandomInt(GlobalMeshes->Meshes.size() - 1);
        index %= AsteroidMeshIds.size();
        GlobalMeshes->Meshes[AsteroidMeshIds[index]].Draw(cmdList);
    }

    void DrawSun(GraphicsContext& ctx, float scale) {
        if (SunId == -1) return;

        ctx.ApplyEffect(Effects->MenuSun);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());

        auto cmdList = ctx.GetCommandList();
        MenuSunShader::Constants constants{};
        constants.Ambient = Color(2.5f, 0.85f, 0.1f) * 300;
        constants.World = Matrix::CreateScale(scale) /** Matrix::CreateRotationX(DirectX::XM_PIDIV2)*/;
        Shaders->MenuSun.SetConstants(cmdList, constants);
        Shaders->MenuSun.SetNoise(cmdList, Render::Materials->Get("noise").Handle());
        GlobalMeshes->Meshes[SunId].Draw(cmdList);
    }

    void DrawStars(GraphicsContext& ctx) {
        auto cmdList = ctx.GetCommandList();
        ctx.ApplyEffect(Effects->Stars);
        //ctx.SetConstantBuffer(0, Adapter->GetTerrainConstants().GetGPUVirtualAddress());
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        Color color;
        Shaders->Stars.SetParameters(cmdList, { color });
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    void DrawAsteroids(GraphicsContext& ctx, int count) {
        float radius = 1000;

        ctx.ApplyEffect(Effects->Asteroid);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());

        for (int i = 0; i < count; i++) {
            auto ix = PcgRandomFloat(i + 1) * (float)Inferno::Clock.GetTotalTimeSeconds() * 0.01f;
            auto iy = PcgRandomFloat(i + 2) * (float)Inferno::Clock.GetTotalTimeSeconds() * 0.01f;
            auto iz = PcgRandomFloat(i + 3) * (float)Inferno::Clock.GetTotalTimeSeconds() * 0.01f;

            Vector3 rotation = { i * 2.31f + ix, -i * 1.1f - iy, i * 4.6f + iz };
            auto scale = PcgRandomFloat(i + 1) - 0.25f;
            scale *= 0.5f;

            float dx = PcgRandomFloat(i);
            float dy = PcgRandomFloat(i + 5);
            float dz = PcgRandomFloat(i + 10);
            Vector3 offset = { -100 + 200 * dx, 0 - 100 + 200 * dy, radius + dz * 100 };
            DrawAsteroid(ctx, offset, rotation, i * DirectX::XM_PI * 2 / count, 1.0f + scale, i);
        }
    }


    void DrawMainMenuBackground(GraphicsContext& ctx) {
        DrawStars(ctx);
        DrawSun(ctx, 100);
        //DrawAsteroids(ctx, 50);
    }

    void CreateMainMenuResources() {
        GenerateAsteroids();

        string extraTextures[] = { "noise" };
        Graphics::LoadTextures(extraTextures);
    }
}
