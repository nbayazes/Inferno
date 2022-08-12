#include "pch.h"
#include "Render.Particles.h"
#include "DataPool.h"
#include "Render.h"
#include "Game.h"

namespace Inferno::Render {
    DataPool<Particle> Particles(Particle::IsAlive, 100);

    void AddParticle(Particle& p, bool randomRotation) {
        auto& vclip = Resources::GetVideoClip(p.Clip);
        p.Life = vclip.PlayTime;
        if (randomRotation)
            p.Rotation = Random() * DirectX::XM_2PI;

        Render::LoadTextureDynamic(p.Clip);
        Particles.Add(p);
    }

    void UpdateParticles(Level& level, float dt) {
        for (auto& p : Particles) {
            if (!Particle::IsAlive(p)) continue;
            p.Life -= dt;

            if (auto parent = level.TryGetObject(p.Parent)) {
                auto pos = Vector3::Lerp(parent->LastPosition, parent->Position, Game::LerpAmount);
                if (p.ParentOffset != Vector3::Zero) {
                    auto rot = Matrix::Lerp(parent->LastRotation, parent->Rotation, Game::LerpAmount);
                    pos += Vector3::Transform(p.ParentOffset, rot);
                }

                p.Position = pos;
            }
        }
    }

    void DrawParticles(Graphics::GraphicsContext& ctx) {
        ctx.BeginEvent(L"Particles");
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
            auto color = p.Color;
            if (p.FadeTime != 0 && p.Life <= p.FadeTime) {
                color.w = 1 - std::clamp((p.FadeTime - p.Life) / p.FadeTime, 0.0f, 1.0f);
            }
            DrawVClip(ctx, vclip, p.Position, p.Radius, color, elapsed, true, p.Rotation, up);
        }

        ctx.EndEvent();
    }
}
