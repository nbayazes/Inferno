#include "pch.h"
#include "Render.Particles.h"
#include "DataPool.h"
#include "Render.h"
#include "Game.h"
#include "Physics.h"
#include "Editor/Editor.Segment.h"

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
            auto position = debris.Transform.Translation() + debris.Velocity * dt;
            //debris.Transform.Translation(debris.Transform.Translation() + debris.Velocity * dt);

            const auto drag = debris.Drag * 5 / 2;
            debris.AngularVelocity *= 1 - drag;
            debris.Transform.Translation(Vector3::Zero);
            debris.Transform = Matrix::CreateFromYawPitchRoll(-debris.AngularVelocity * dt * DirectX::XM_2PI) * debris.Transform;
            debris.Transform.Translation(position);

            LevelHit hit;
            BoundingCapsule capsule = { 
                .A = debris.PrevTransform.Translation(), 
                .B = debris.Transform.Translation(), 
                .Radius = debris.Radius / 2 
            };

            if (IntersectLevelDebris(Game::Level, capsule, debris.Segment, hit)) {
                debris.Life = -1; // destroy on contact
                // scorch marks on walls?
            }

            if (!Editor::PointInSegment(Game::Level, debris.Segment, position)) {
                auto id = Editor::FindContainingSegment(Game::Level, position);
                if (id != SegID::None) debris.Segment = id;
            }

            if (debris.Life < 0) {
                ExplosionInfo e;
                e.MinRadius = debris.Radius * 1.0f;
                e.MaxRadius = debris.Radius * 1.45f;
                e.Position = debris.PrevTransform.Translation();
                e.Variance = debris.Radius * 1.0f;
                e.Instances = 2;
                e.Segment = debris.Segment;
                e.MinDelay = 0.15f;
                e.MaxDelay = 0.3f;
                CreateExplosion(e);
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

    DataPool<ExplosionInfo> Explosions(ExplosionInfo::IsAlive, 50);

    void CreateExplosion(ExplosionInfo& e) {
        if (e.InitialDelay < 0) e.InitialDelay = 0;
        if (e.Instances < 0) e.Instances = 1;
        Explosions.Add(e);
    }

    void UpdateExplosions(float dt) {
        for (auto& expl : Explosions) {
            if (expl.InitialDelay < 0) continue;
            expl.InitialDelay -= dt;
            if (expl.InitialDelay > 0) continue;

            if (expl.Sound != SoundID::None) {
                fmt::print("playing expl sound\n");
                Sound::Sound3D sound(expl.Position, expl.Segment);
                sound.Resource = Resources::GetSoundResource(expl.Sound);
                //sound.Source = expl.Parent; // no parent so all nearby explosions merge
                Sound::Play(sound);
            }

            for (int i = 0; i < expl.Instances; i++) {
                Render::Particle p{};
                p.Position = expl.Position;
                if (expl.Variance > 0)
                    p.Position += Vector3(RandomN11() * expl.Variance, RandomN11() * expl.Variance, RandomN11() * expl.Variance);

                p.Radius = expl.MinRadius + Random() * (expl.MaxRadius - expl.MinRadius);
                p.Clip = expl.Clip;
                p.Color = expl.Color;
                p.FadeTime = expl.FadeTime;
                if (expl.Instances > 1 && i > 0)
                    p.Delay = expl.MinDelay + Random() * (expl.MaxDelay - expl.MinDelay);
                Render::AddParticle(p);
            }
        }
    }

}
