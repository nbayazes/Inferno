#include "pch.h"
#include "Render.Particles.h"
#include "DataPool.h"
#include "Render.h"
#include "Game.h"

namespace Inferno::Render {
    DataPool<Particle> Particles(Particle::IsAlive, 100);
    DataPool<ParticleEmitter> ParticleEmitters(ParticleEmitter::IsAlive, 20);

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
            if ((p.Delay -= dt) > 0) continue;
            p.Life -= dt;

            if (auto parent = level.TryGetObject(p.Parent)) {
                auto pos = parent->GetPosition(Game::LerpAmount);
                if (p.ParentOffset != Vector3::Zero) {
                    pos += Vector3::Transform(p.ParentOffset, parent->GetRotation(Game::LerpAmount));
                }

                p.Position = pos;
            }

            if (!Particle::IsAlive(p))
                p = {};
        }
    }

    void QueueParticles() {
        int liveParticles = 0;
        for (auto& p : Particles) {
            if (!Particle::IsAlive(p)) continue;
            auto depth = Vector3::DistanceSquared(Render::Camera.Position, p.Position) * 0.98f - 100;
            if (p.Delay > 0) continue;
            liveParticles++;
            RenderCommand cmd(&p, depth);
            SubmitToTransparentQueue(cmd);
        }

        //fmt::print("live particles: {}", liveParticles);
    }

    void DrawParticles(Graphics::GraphicsContext& ctx) {
        for (auto& p : Particles) {
            if (!Particle::IsAlive(p)) continue;
            if (p.Delay > 0) continue;
            auto& vclip = Resources::GetVideoClip(p.Clip);
            auto elapsed = vclip.PlayTime - p.Life;

            auto* up = p.Up == Vector3::Zero ? nullptr : &p.Up;
            auto color = p.Color;
            if (p.FadeTime != 0 && p.Life <= p.FadeTime) {
                color.w = 1 - std::clamp((p.FadeTime - p.Life) / p.FadeTime, 0.0f, 1.0f);
            }
            DrawVClip(ctx, vclip, p.Position, p.Radius, color, elapsed, true, p.Rotation, up);
        }

    }

    void AddEmitter(ParticleEmitterInfo& info, size_t capacity) {
        Render::LoadTextureDynamic(info.Clip);
        ParticleEmitter emitter(info, capacity);
        ParticleEmitters.Add(emitter);
    }

    void ParticleEmitter::Update(float dt) {
        if (!ParticleEmitter::IsAlive(*this)) return;
        if ((_startDelay -= dt) > 0) return;

        _life -= dt;

        if ((_info.MaxDelay == 0 && _info.MinDelay == 0) && _info.ParticlesToSpawn > 0) {
            // Create all particles at once if delay is zero
            while (_info.ParticlesToSpawn-- > 0) {
                AddParticle();
            }
        }
        else {
            _spawnTimer -= dt;
            if (_spawnTimer < 0) {
                AddParticle();
                _spawnTimer = _info.MinDelay + Random() * (_info.MaxDelay - _info.MinDelay);
            }
        }
    }

    void UpdateEmitters(float dt) {
        for (auto& emitter : ParticleEmitters) {
            if (!ParticleEmitter::IsAlive(emitter)) continue;
            emitter.Update(dt);
            
            auto depth = Vector3::DistanceSquared(Render::Camera.Position, emitter.Position) * 0.98f - 100;
            RenderCommand cmd(&emitter, depth);
            SubmitToTransparentQueue(cmd);
        }
    }
}
