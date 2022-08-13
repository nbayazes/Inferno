#include "pch.h"
#include "Render.Particles.h"
#include "DataPool.h"
#include "Render.h"
#include "Game.h"
#include "Physics.h"

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
        for (auto& p : Particles) {
            if (!Particle::IsAlive(p)) continue;
            auto depth = Vector3::DistanceSquared(Render::Camera.Position, p.Position) * 0.98f - 100;
            if (p.Delay > 0) continue;
            RenderCommand cmd(&p, depth);
            QueueTransparent(cmd);
        }
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
            QueueTransparent(cmd);
        }
    }

    DataPool<Debris> DebrisPool(Debris::IsAlive, 100);

    void AddDebris(Debris& debris) {
        DebrisPool.Add(debris);
    }

    void UpdateDebris(float dt) {
        for (auto& debris : DebrisPool) {
            if (!Debris::IsAlive(debris)) continue;
            debris.Velocity *= 1 - debris.Drag;
            debris.Life -= dt;
            debris.PrevTransform = debris.Transform;
            auto translation = debris.Transform.Translation() + debris.Velocity * dt;
            //debris.Transform.Translation(debris.Transform.Translation() + debris.Velocity * dt);

            const auto drag = debris.Drag * 5 / 2;
            debris.AngularVelocity *= 1 - drag;
            debris.Transform.Translation(Vector3::Zero);
            debris.Transform = Matrix::CreateFromYawPitchRoll(-debris.AngularVelocity * dt * DirectX::XM_2PI) * debris.Transform;
            debris.Transform.Translation(translation);

            DirectX::BoundingSphere bounds(debris.Transform.Translation(), debris.Radius / 2);
            LevelHit hit;
            if (IntersectLevelDebris(Game::Level, bounds, debris.Segment, hit)) {
                debris.Life = -1; // destroy on contact
                // scorch marks on walls?
            }

            if (debris.Life < 0) {
                Render::Particle p{};
                //p.Position = Vector3::Lerp(debris.PrevTransform.Translation(), debris.Transform.Translation(), Game::LerpAmount);
                p.Position = debris.PrevTransform.Translation();
                p.Radius = debris.Radius * 1.5f;
                p.Clip = VClipID(2); // small explosion, randomize?
                AddParticle(p);
            }
        }
    }

    void QueueDebris() {
        for (auto& debris : DebrisPool) {
            if (!Debris::IsAlive(debris)) continue;
            auto depth = Vector3::DistanceSquared(Render::Camera.Position, debris.Transform.Translation()) * 0.98f - 100;
            RenderCommand cmd(&debris, depth);
            QueueOpaque(cmd);
        }
    }
}
