#include "pch.h"
#include "Render.Particles.h"
#include "DataPool.h"
#include "Render.h"

namespace Inferno::Render {
    //using namespace DirectX;

    DataPool<Particle> Particles(Particle::IsAlive, 100);

    void AddParticle(Particle& p, bool randomRotation) {
        auto& vclip = Resources::GetVideoClip(p.Clip);
        p.Life = vclip.PlayTime;
        if (randomRotation)
            p.Rotation = Random() * DirectX::XM_2PI;
        Particles.Add(p);
        Render::LoadTextureDynamic(p.Clip);
    }

    void UpdateParticles(float dt) {
        for (auto& p : Particles) {
            if (!Particle::IsAlive(p)) continue;
            p.Life -= dt;
        }
    }

    void DrawParticles(ID3D12GraphicsCommandList* cmd) {
        //auto& effect = Effects->SpriteAdditive;
        //effect.Apply(cmd);
        //effect.Shader->SetWorldViewProjection(cmd, ViewProjection);
        //auto sampler = Render::GetClampedTextureSampler();
        //effect.Shader->SetSampler(cmd, sampler);

        for (auto& p : Particles) {
            if (!Particle::IsAlive(p)) continue;
            auto& vclip = Resources::GetVideoClip(p.Clip);
            auto elapsed = vclip.PlayTime - p.Life;

            auto* up = p.Up == Vector3::Zero ? nullptr : &p.Up;
            DrawVClip(cmd, vclip, p.Position, p.Radius, p.Color, elapsed, true, p.Rotation, up);

            //auto frame = vclip.NumFrames - (int)(elapsed / vclip.FrameTime) % vclip.NumFrames - 1;
            //auto tid = vclip.Frames[frame];
            //auto billboard = Matrix::CreateRotationZ(p.Rotation) * Matrix::CreateBillboard(p.Position, Camera.Position, Camera.Up);

            //// create quad and transform it
            //auto& ti = Resources::GetTextureInfo(tid);
            //auto ratio = (float)ti.Height / (float)ti.Width;
            //const auto r = p.Radius;
            //const auto h = r * ratio;
            //const auto w = r;
            //auto p0 = Vector3::Transform({ -w, h, 0 }, billboard); // bl
            //auto p1 = Vector3::Transform({ w, h, 0 }, billboard); // br
            //auto p2 = Vector3::Transform({ w, -h, 0 }, billboard); // tr
            //auto p3 = Vector3::Transform({ -w, -h, 0 }, billboard); // tl

            //ObjectVertex v0(p0, { 0, 0 }, p.Color);
            //ObjectVertex v1(p1, { 1, 0 }, p.Color);
            //ObjectVertex v2(p2, { 1, 1 }, p.Color);
            //ObjectVertex v3(p3, { 0, 1 }, p.Color);

            //auto& material = Materials->Get(tid);
            //effect.Shader->SetDiffuse(cmd, material.Handles[0]);

            //DrawCalls++;
            //g_SpriteBatch->Begin(cmd);
            //g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
            //g_SpriteBatch->End();
        }
    }
}
