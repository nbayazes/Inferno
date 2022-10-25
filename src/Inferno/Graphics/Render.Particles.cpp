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
            auto depth = GetRenderDepth(p.Position);
            if (p.Delay > 0) continue;
            RenderCommand cmd(&p, depth);
            QueueTransparent(cmd);
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

            auto depth = GetRenderDepth(emitter.Position);
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
            auto depth = GetRenderDepth(debris.Transform.Translation());
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
                Sound3D sound(expl.Position, expl.Segment);
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

    // gets a random point at a given radius, intersecting the level
    Vector3 GetRandomPoint(const Vector3& pos, SegID seg, float radius) {
        //Vector3 end;
        LevelHit hit;
        auto dir = RandomVector(1);
        dir.Normalize();

        if (IntersectLevel(Game::Level, { pos, dir }, seg, radius, hit))
            return hit.Point;
        else
            return pos + dir * radius;
    }

    // Beam code based on xash3d-fwgs gl_beams.c

    struct Beam {
        SegID Segment;
        List<ObjectVertex> Mesh;
        float NextUpdate = 0;
        BeamInfo Info;
    };

    DataPool<BeamInfo> Beams(BeamInfo::IsAlive, 50);

    void AddBeam(BeamInfo& beam) {
        beam.Segment = Editor::FindContainingSegment(Game::Level, beam.Start);
        std::array tex = { beam.Texture };
        Render::Materials->LoadTextures(tex);

        if (beam.RandomEnd)
            beam.End = GetRandomPoint(beam.Start, beam.Segment, beam.Radius);

        beam.Runtime.Length = (beam.Start - beam.End).Length();
        Beams.Add(beam);
    }



    Vector3 GetBeamNormal(const Vector3& start, const Vector3 end) {
        auto tangent = start - end;
        auto dirToBeam = start - Render::Camera.Position;
        auto normal = dirToBeam.Cross(tangent);
        normal.Normalize();
        return normal;
    }

    Vector2 SinCos(float x) {
        return { sin(x), cos(x) };
    };

    // Fractal noise generator, power of 2 wavelength
    void FracNoise(span<float> noise) {
        if (noise.size() < 2) return;
        int div2 = noise.size() >> 1;

        // noise is normalized to +/- scale
        noise[div2] = (noise.front() + noise.back()) * 0.5f + noise.size() * RandomN11() * 0.125f;

        if (div2 > 1) {
            FracNoise(noise.subspan(0, div2 + 1)); // -1 ?
            FracNoise(noise.subspan(div2));
        }
    }

    void SineNoise(span<float> noise) {
        float freq = 0;
        float step = DirectX::XM_PI / (float)noise.size();

        for (auto& n : noise) {
            n = sin(freq);
            freq += step;
        }
    }

    void UpdateBeams(float dt) {
        for (auto& beam : Beams) {
            int step = beam.Life / 0.1f;
            // seed random based on step? cache mesh?
        }
    }


    //void DrawBeamOriginal(BeamInfo& beam) {
    //    auto& material = Render::Materials->GetOutrageMaterial(beam.Texture);
    //    effect.Shader->SetDiffuse(ctx.CommandList(), material.Handles[0]);

    //    // create a path between start and end, then apply noise
    //    auto start = beam.Start;
    //    auto& end = beam.End.value();
    //    auto delta = start - end;
    //    auto length = delta.Length();
    //    //delta.Normalize();

    //    DrawCalls++;
    //    g_SpriteBatch->Begin(ctx.CommandList());

    //    auto h = beam.Width / 2;

    //    auto normal = GetBeamNormal(start, end); // todo: average with previous segment
    //    auto up = normal * h;
    //    ObjectVertex v0{ start + up, { 0, 0 }, beam.Color };
    //    ObjectVertex v1{ start - up, { 1, 0 }, beam.Color };
    //    ObjectVertex v2{ end - up, { 1, 1 * vScale}, beam.Color };
    //    ObjectVertex v3{ end + up, { 0, 1 * vScale}, beam.Color };

    //    g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
    //}

    Vector3 GetBeamPerpendicular(const Vector3 delta) {
        Vector3 dir;
        delta.Normalize(dir);
        auto perp = Camera.GetForward().Cross(dir);
        perp.Normalize();
        return perp;

        //auto invLen = delta.Length();
        //if (invLen == 0) return {};

        //invLen = 1.0f / invLen;
        //auto center = delta * invLen;
        //auto perp = Camera.GetForward().Cross(center);
        //perp.Normalize();
        //return perp;
    }

    void DrawBeams(Graphics::GraphicsContext& ctx) {
        auto& effect = Effects->SpriteAdditive;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
        effect.Shader->SetDepthTexture(ctx.CommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
        effect.Shader->SetSampler(ctx.CommandList(), Render::GetTextureSampler());

        for (auto& beam : Beams) {
            //beam.Life -= dt;

            if (!BeamInfo::IsAlive(beam)) continue;

            beam.Time += Render::FrameTime;
            auto& noise = beam.Runtime.Noise;

            auto delta = beam.End - beam.Start;
            auto length = delta.Length();
            if (length < 1) continue; // don't draw really short beams

            // todo: if start or end object are set, update endpoints

            // DrawSegs()
            auto vScale = length / beam.Width * beam.Scale;
            auto scale = beam.Amplitude;

            int segments = (int)(length / (beam.Width * 0.5 * 1.414)) + 1;
            segments = std::clamp(segments, 2, 64);
            auto div = 1.0f / (segments - 1);

            auto vLast = std::fmod(beam.Time * beam.ScrollSpeed, 1);
            if (beam.SineNoise) {
                if (segments < 16) {
                    segments = 16;
                    div = 1.0f / (segments - 1);
                }
                scale *= 100;
                length = segments * 0.1f;

            }
            else {
                scale *= length * 2;
            }

            noise.resize(segments);

            if (beam.Amplitude > 0 && Render::ElapsedTime > beam.Runtime.NextUpdate) {
                if (beam.SineNoise)
                    SineNoise(noise);
                else
                    FracNoise(noise);

                beam.Runtime.NextUpdate = Render::ElapsedTime + beam.Frequency;
            }

            //auto noiseStep = (int)((float)(beam.Runtime.Noise.size() - 1) * div * 65536.0f);
            int noiseIndex = 0;

            auto perp1 = GetBeamPerpendicular(delta);
            auto center = (beam.End + beam.Start) / 2;
            Vector3 dir;
            delta.Normalize(dir);
            auto billboard = Matrix::CreateConstrainedBillboard(center, Camera.Position, dir);
            perp1 = billboard.Up();

            // if (flags.FadeIn) alpha = 0;

            struct BeamSeg {
                Vector3 pos;
                float texcoord;
                float width;
            };

            BeamSeg curSeg{};
            //int segsDrawn = 0;
            auto vStep = length / 20 * div * beam.Scale;

            auto& material = Render::Materials->GetOutrageMaterial(beam.Texture);
            effect.Shader->SetDiffuse(ctx.CommandList(), material.Handles[0]);
            DrawCalls++;
            g_SpriteBatch->Begin(ctx.CommandList());

            Vector3 prevNormal;
            Vector3 prevUp;

            auto tangent = GetBeamNormal(beam.Start, beam.End);

            for (int i = 0; i < segments; i++) {

                BeamSeg nextSeg{};
                auto fraction = i * div;

                nextSeg.pos = beam.Start + delta * fraction;

                if (beam.Amplitude != 0) {
                    //auto factor = beam.Runtime.Noise[noiseIndex >> 16] * beam.Amplitude;
                    auto factor = noise[i] * beam.Amplitude;

                    if (beam.SineNoise) {
                        // rotate the noise along the perpendicluar axis a bit to keep the bolt from looking diagonal
                        auto c = SinCos(fraction * DirectX::XM_PI * length + beam.Time);
                        nextSeg.pos += Render::Camera.Up * factor * c.x;
                        nextSeg.pos += Render::Camera.GetRight() * factor * c.y;
                    }
                    else {
                        //nextSeg.pos += perp1 * factor;
                        nextSeg.pos += tangent * factor;
                    }
                }

                //nextSeg.width = beam.Width * 2.0f;
                nextSeg.texcoord = vLast;

                if (i > 0) {
                    Vector3 avgNormal;
                    auto normal = GetBeamNormal(curSeg.pos, nextSeg.pos);

                    if (i > 1) {
                        // Average with previous normal
                        avgNormal = (normal + prevNormal) * 0.5f;
                        avgNormal.Normalize();
                    }
                    else {
                        avgNormal = normal;
                    }

                    prevNormal = normal;

                    // VectorMA: d = a + c * b
                    // VectorMA( source, fraction, delta, nextSeg.pos );

                    auto h = beam.Width / 2;

                    // draw rectangular segment
                    auto start = curSeg.pos;
                    auto end = nextSeg.pos;
                    auto up = avgNormal * beam.Width * 0.5f;
                    if (i == 1) prevUp = up;

                    ObjectVertex v0{ start + prevUp, { 0, curSeg.texcoord }, beam.Color };
                    ObjectVertex v1{ start - prevUp, { 1, curSeg.texcoord }, beam.Color };
                    ObjectVertex v2{ end - up, { 1, nextSeg.texcoord }, beam.Color };
                    ObjectVertex v3{ end + up, { 0, nextSeg.texcoord }, beam.Color };

                    g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
                    prevUp = up;
                }

                curSeg = nextSeg;
                //segsDrawn++;

                //if (i == segments - 1) {
                //    // draw last segment
                //}

                vLast += vStep; // next segment tex V coord
                //noiseIndex += noiseStep;




                // create a path between start and end, then apply noise
                //auto start = beam.Start;
                //auto& end = beam.End.value();
                //auto delta = start - end;
                //auto length = delta.Length();
                //delta.Normalize();




                //if (beam.Noise == 0) {
                //}
                //else {

                //    // generate mesh based on noise

                //    auto up = Render::Camera.Up * (beam.Width / 2);
                //    ObjectVertex v0{ start + up, { 0, 0 }, beam.Color };
                //    ObjectVertex v1{ start - up, { 0, 1 }, beam.Color };
                //    ObjectVertex v2{ end + up, { 1, 0 }, beam.Color };
                //    ObjectVertex v3{ end + up, { 1, 1 }, beam.Color };

                //    g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
                //}
            }

            g_SpriteBatch->End();
        }

        //void QueueBeams() {
        //    for (auto& beam : Beams) {
        //        auto depth = GetRenderDepth(beam.Start);
        //        RenderCommand cmd(&beam, depth);
        //        QueueTransparent(cmd);
        //    }
        //}
    }
}