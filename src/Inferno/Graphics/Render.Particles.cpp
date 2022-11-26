#include "pch.h"
#include "Render.Particles.h"
#include "DataPool.h"
#include "Render.h"
#include "Game.h"
#include "Physics.h"
#include "Render.Queue.h"
#include "Editor/Editor.Segment.h"

namespace Inferno::Render {
    using Graphics::GraphicsContext;

    namespace {
        DataPool<BeamInfo> Beams(BeamInfo::IsAlive, 50);
        Array<DecalInfo, 100> Decals;
        DataPool<ExplosionInfo> Explosions(ExplosionInfo::IsAlive, 50);
        DataPool<ParticleEmitter> ParticleEmitters(ParticleEmitter::IsAlive, 10);
        uint16 DecalIndex = 0;
        List<List<Ptr<EffectBase>>> SegmentEffects; // equals segment count
    }

    span<Ptr<EffectBase>> GetEffectsInSegment(SegID id) {
        return SegmentEffects[(int)id];
    }

    void AddEffect(Ptr<EffectBase> e) {
        assert(e->Segment > SegID::None);
        auto seg = (int)e->Segment;

        for (auto& effect : SegmentEffects[seg]) {
            if (!effect || !effect->IsAlive()) {
                effect = std::move(e);
                return;
            }
        }

        SegmentEffects[seg].push_back(std::move(e));
    }

    void AddParticle(Particle& p, SegID seg) {
        auto& vclip = Resources::GetVideoClip(p.Clip);
        p.Life = vclip.PlayTime;
        p.Segment = seg;
        if (p.RandomRotation)
            p.Rotation = Random() * DirectX::XM_2PI;

        Render::LoadTextureDynamic(p.Clip);
        AddEffect(MakePtr<Particle>(p));
    }

    void AddEmitter(const ParticleEmitterInfo& info, SegID) {
        //info.Segment = seg;
        Render::LoadTextureDynamic(info.Clip);
        ParticleEmitter emitter(info, 100);
        ParticleEmitters.Add(emitter);
    }

    void Particle::Update(float dt) {
        if ((Delay -= dt) > 0) return;
        Life -= dt;

        if (auto parent = Game::Level.TryGetObject(Parent)) {
            auto pos = parent->GetPosition(Game::LerpAmount);
            if (ParentOffset != Vector3::Zero)
                pos += Vector3::Transform(ParentOffset, parent->GetRotation(Game::LerpAmount));

            Position = pos;
        }
    }

    void Particle::Draw(Graphics::GraphicsContext& ctx) {
        if (Delay > 0) return;

        auto& vclip = Resources::GetVideoClip(Clip);
        auto elapsed = vclip.PlayTime - Life;

        auto* up = Up == Vector3::Zero ? nullptr : &Up;
        auto color = Color;
        if (FadeTime != 0 && Life <= FadeTime) {
            color.w = 1 - std::clamp((FadeTime - Life) / FadeTime, 0.0f, 1.0f);
        }
        auto tid = vclip.GetFrame(elapsed);
        DrawBillboard(ctx, tid, Position, Radius, color, true, Rotation, up);
    }

    void ParticleEmitter::Update(float dt) {
        if (!ParticleEmitter::IsAlive(*this)) return;
        if ((_startDelay -= dt) > 0) return;

        Life -= dt;

        if (_info.MaxDelay == 0 && _info.MinDelay == 0 && _info.ParticlesToSpawn > 0) {
            // Create all particles at once if delay is zero
            while (_info.ParticlesToSpawn-- > 0) {
                _particles.Add(_info.CreateParticle());
            }
        }
        else {
            _spawnTimer -= dt;
            if (_spawnTimer < 0) {
                _particles.Add(_info.CreateParticle());
                _spawnTimer = _info.MinDelay + Random() * (_info.MaxDelay - _info.MinDelay);
            }
        }
    }

    //void UpdateEmitters(float dt) {
    //    for (auto& emitter : ParticleEmitters) {
    //        if (!ParticleEmitter::IsAlive(emitter)) continue;
    //        emitter.Update(dt);

    //        auto depth = GetRenderDepth(emitter.Position);
    //        RenderCommand cmd(&emitter, depth);
    //        //QueueTransparent(cmd);
    //    }
    //}

    void Debris::Draw(Graphics::GraphicsContext& ctx) {
        auto& model = Resources::GetModel(Model);
        if (model.DataSize == 0) return;
        if (!Seq::inRange(model.Submodels, Submodel)) return;
        auto& meshHandle = GetMeshHandle(Model);

        auto& effect = Effects->Object;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
        auto cmdList = ctx.CommandList();

        effect.Shader->SetSampler(cmdList, GetTextureSampler());
        auto& seg = Game::Level.GetSegment(Segment);
        ObjectShader::Constants constants = {};
        constants.Ambient = Settings::Editor.RenderMode == RenderMode::Shaded ? seg.VolumeLight : Color(1, 1, 1);
        constants.EmissiveLight = Vector4::Zero;

        Matrix transform = Matrix::Lerp(PrevTransform, Transform, Game::LerpAmount);
        //transform.Forward(-transform.Forward()); // flip z axis to correct for LH models
        constants.World = transform;
        effect.Shader->SetConstants(cmdList, constants);

        // get the mesh associated with the submodel
        auto& subMesh = meshHandle.Meshes[Submodel];

        for (int i = 0; i < subMesh.size(); i++) {
            auto mesh = subMesh[i];
            if (!mesh) continue;

            TexID tid = TexOverride;
            if (tid == TexID::None)
                tid = mesh->EffectClip == EClipID::None ? mesh->Texture : Resources::GetEffectClip(mesh->EffectClip).VClip.GetFrame(ElapsedTime);

            const Material2D& material = tid == TexID::None ? Materials->White : Materials->Get(tid);
            effect.Shader->SetMaterial(cmdList, material);

            cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
            cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
            cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
            Stats::DrawCalls++;
        }

    }

    void Debris::DepthPrepass(Graphics::GraphicsContext& ctx) {
        auto& model = Resources::GetModel(Model);
        if (model.DataSize == 0) return;
        if (!Seq::inRange(model.Submodels, Submodel)) return;
        auto& meshHandle = GetMeshHandle(Model);
        auto& effect = Effects->DepthObject;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
        auto cmdList = ctx.CommandList();

        Matrix transform = Matrix::Lerp(PrevTransform, Transform, Game::LerpAmount);
        //transform.Forward(-transform.Forward()); // flip z axis to correct for LH models

        ObjectDepthShader::Constants constants = {};
        constants.World = transform;

        effect.Shader->SetConstants(cmdList, constants);

        // get the mesh associated with the submodel
        auto& subMesh = meshHandle.Meshes[Submodel];

        for (int i = 0; i < subMesh.size(); i++) {
            auto mesh = subMesh[i];
            if (!mesh) continue;

            cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
            cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
            cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
            Stats::DrawCalls++;
        }
    }

    void Debris::FixedUpdate(float dt) {
        Velocity *= 1 - Drag;
        Life -= dt;
        PrevTransform = Transform;
        auto position = Transform.Translation() + Velocity * dt;
        //Transform.Translation(Transform.Translation() + Velocity * dt);

        const auto drag = Drag * 5 / 2;
        AngularVelocity *= 1 - drag;
        Transform.Translation(Vector3::Zero);
        Transform = Matrix::CreateFromYawPitchRoll(-AngularVelocity * dt * DirectX::XM_2PI) * Transform;
        Transform.Translation(position);

        LevelHit hit;
        BoundingCapsule capsule = {
            .A = PrevTransform.Translation(),
            .B = Transform.Translation(),
            .Radius = Radius / 2
        };

        if (IntersectLevelDebris(Game::Level, capsule, Segment, hit)) {
            Life = -1; // destroy on contact
            // todo: scorch marks on walls
        }

        if (!Editor::PointInSegment(Game::Level, Segment, position)) {
            auto id = Editor::FindContainingSegment(Game::Level, position);
            if (id != SegID::None) Segment = id;
        }

        if (Life < 0) {
            ExplosionInfo e;
            e.MinRadius = Radius * 1.0f;
            e.MaxRadius = Radius * 1.45f;
            e.Position = PrevTransform.Translation();
            e.Variance = Radius * 1.0f;
            e.Instances = 2;
            e.Segment = Segment;
            e.MinDelay = 0.15f;
            e.MaxDelay = 0.3f;
            CreateExplosion(e);
        }
    }

    void AddDebris(Debris& debris, SegID seg) {
        debris.Segment = seg;
        AddEffect(MakePtr<Debris>(debris));
    }

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
                //fmt::print("playing expl sound\n");
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

                Render::AddParticle(p, expl.Segment);
            }
        }
    }

    // gets a random point at a given radius, intersecting the level
    Vector3 GetRandomPoint(const Vector3& pos, SegID seg, float radius) {
        //Vector3 end;
        LevelHit hit;
        auto dir = RandomVector(1);
        dir.Normalize();

        if (IntersectLevel(Game::Level, { pos, dir }, seg, radius, false, hit))
            return hit.Point;
        else
            return pos + dir * radius;
    }

    // Beam code based on xash3d-fwgs gl_beams.c

    struct Beam {
        SegID Segment = SegID::None;
        List<ObjectVertex> Mesh{};
        float NextUpdate = 0;
        BeamInfo Info;
    };

    void AddBeam(BeamInfo& beam) {
        beam.Segment = Editor::FindContainingSegment(Game::Level, beam.Start);
        std::array tex = { beam.Texture };
        Render::Materials->LoadTextures(tex);

        if (beam.RandomEnd)
            beam.End = GetRandomPoint(beam.Start, beam.Segment, beam.Radius);

        beam.Runtime.Length = (beam.Start - beam.End).Length();
        //Beams.Add(beam);
    }

    // returns a vector perpendicular to the camera and the start/end points
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
    void FractalNoise(span<float> noise) {
        if (noise.size() < 2) return;
        int div2 = (int)noise.size() >> 1;

        // noise is normalized to +/- scale
        noise[div2] = (noise.front() + noise.back()) * 0.5f + noise.size() * RandomN11() * 0.125f;

        if (div2 > 1) {
            FractalNoise(noise.subspan(0, div2 + 1)); // -1 ?
            FractalNoise(noise.subspan(div2));
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

    Vector3 GetBeamPerpendicular(const Vector3 delta) {
        Vector3 dir;
        delta.Normalize(dir);
        auto perp = Camera.GetForward().Cross(dir);
        perp.Normalize();
        return perp;
    }


    void DrawBeams(Graphics::GraphicsContext& ctx) {
        auto& effect = Effects->SpriteAdditive;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
        effect.Shader->SetDepthTexture(ctx.CommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
        effect.Shader->SetSampler(ctx.CommandList(), Render::Heaps->States.AnisotropicWrap());

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
            //auto vScale = length / beam.Width * beam.Scale;
            auto scale = beam.Amplitude;

            int segments = (int)(length / (beam.Width * 0.5 * 1.414)) + 1;
            segments = std::clamp(segments, 2, 64);
            auto div = 1.0f / (segments - 1);

            auto vLast = std::fmodf(beam.Time * beam.ScrollSpeed, 1);
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

            if (beam.Amplitude > 0 && (float)Render::ElapsedTime > beam.Runtime.NextUpdate) {
                if (beam.SineNoise)
                    SineNoise(noise);
                else
                    FractalNoise(noise);

                beam.Runtime.NextUpdate = (float)Render::ElapsedTime + beam.Frequency;
            }

            //auto perp1 = GetBeamPerpendicular(delta);
            //auto center = (beam.End + beam.Start) / 2;
            //Vector3 dir;
            //delta.Normalize(dir);
            //auto billboard = Matrix::CreateConstrainedBillboard(center, Camera.Position, dir);
            //auto perp1 = billboard.Up();

            // if (flags.FadeIn) alpha = 0;

            struct BeamSeg {
                Vector3 pos;
                float texcoord;
            };

            BeamSeg curSeg{};
            //int segsDrawn = 0;
            auto vStep = length / 20 * div * beam.Scale;

            auto& material = Render::Materials->Get(beam.Texture);
            effect.Shader->SetDiffuse(ctx.CommandList(), material.Handles[0]);
            Stats::DrawCalls++;
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
                vLast += vStep; // next segment tex V coord
            }

            g_SpriteBatch->End();
        }
    }

    //void QueueBeams() {
    //    for (auto& beam : Beams) {
    //        auto depth = GetRenderDepth(beam.Start);
    //        RenderCommand cmd(&beam, depth);
    //        QueueTransparent(cmd);
    //    }
    //}

    void TracerInfo::Update(float dt) {
        Life -= dt;
        auto parentWasLive = ParentIsLive;

        const auto obj = Game::Level.TryGetObject(Parent);

        if (obj && obj->Signature == Signature) {
            ParentIsLive = obj->IsAlive();
            End = obj->Position;
            if (ParentIsLive)
                Life = 1;
        }
        else {
            ParentIsLive = false;
        }

        parentWasLive = parentWasLive && !ParentIsLive;
        if (parentWasLive)
            Life = FadeSpeed;
    }

    void TracerInfo::Draw(Graphics::GraphicsContext& ctx) {
        auto& effect = Effects->SpriteAdditive;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
        effect.Shader->SetDepthTexture(ctx.CommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
        effect.Shader->SetSampler(ctx.CommandList(), Render::Heaps->States.AnisotropicClamp());

        const auto delta = Position - End;
        const auto dist = delta.Length();

        if (dist < Length + 2)
            return; // don't draw tracers that are too short

        // Fade tracer in or out based on parent being alive
        auto fadeSpeed = FadeSpeed > 0 ? Render::FrameTime / FadeSpeed : 1;
        if (ParentIsLive)
            Fade += fadeSpeed;
        else
            Fade -= fadeSpeed;

        Fade = std::clamp(Fade, 0.0f, 1.0f);

        Vector3 dir;
        delta.Normalize(dir);

        const auto lenMult = ParentIsLive ? 1 : Fade;
        const auto len = std::min(dist, Length);
        const auto start = End + dir * len * lenMult;
        const auto end = End;

        const auto normal = GetBeamNormal(start, End);

        // draw rectangular segment
        const auto halfWidth = Width * 0.5f;
        auto up = normal * halfWidth;
        auto color = Color;
        color.w *= Fade;

        if (!Texture.empty()) {
            auto& material = Render::Materials->Get(Texture);
            effect.Shader->SetDiffuse(ctx.CommandList(), material.Handles[0]);
            g_SpriteBatch->Begin(ctx.CommandList());

            ObjectVertex v0{ start + up, { 0, 0 }, color };
            ObjectVertex v1{ start - up, { 1, 0 }, color };
            ObjectVertex v2{ end - up, { 1, 1 }, color };
            ObjectVertex v3{ end + up, { 0, 1 }, color };
            g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
            g_SpriteBatch->End();
            Stats::DrawCalls++;
        }

        if (!BlobTexture.empty() && dist > Length) {
            auto& material = Render::Materials->Get(BlobTexture);
            effect.Shader->SetDiffuse(ctx.CommandList(), material.Handles[0]);
            g_SpriteBatch->Begin(ctx.CommandList());

            auto right = Render::Camera.GetRight() * halfWidth;
            up = Render::Camera.Up * halfWidth;
            constexpr float BLOB_OFFSET = 0.25f; // tracer textures are thickest about a quarter from the end
            auto blob = End + dir * Length * BLOB_OFFSET * lenMult;

            ObjectVertex v0{ blob + up - right, { 0, 0 }, color };
            ObjectVertex v1{ blob - up - right, { 1, 0 }, color };
            ObjectVertex v2{ blob - up + right, { 1, 1 }, color };
            ObjectVertex v3{ blob + up + right, { 0, 1 }, color };
            g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
            g_SpriteBatch->End();
            Stats::DrawCalls++;
        }
    }

    void AddTracer(TracerInfo& tracer, SegID seg) {
        std::array tex = { tracer.Texture, tracer.BlobTexture };
        Render::Materials->LoadTextures(tex);
        tracer.Segment = seg;

        assert(tracer.Parent != ObjID::None);

        if (auto obj = Game::Level.TryGetObject(tracer.Parent)) {
            tracer.Position = obj->Position;
            tracer.Signature = obj->Signature;
        }

        tracer.Life = 1;
        AddEffect(MakePtr<TracerInfo>(tracer));
    }

    void AddDecal(DecalInfo& decal) {
        std::array tex = { decal.Texture };
        Render::Materials->LoadTextures(tex);

        decal.Life = FLT_MAX;
        Decals[DecalIndex++] = decal;

        if (DecalIndex >= Decals.size())
            DecalIndex = 0;
    }

    void DrawDecals(Graphics::GraphicsContext& ctx) {
        auto& effect = Effects->SpriteMultiply;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
        effect.Shader->SetDepthTexture(ctx.CommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
        effect.Shader->SetSampler(ctx.CommandList(), Render::Heaps->States.AnisotropicClamp());

        for (auto& decal : Decals) {
            decal.Life -= Render::FrameTime;
            if (decal.Life <= 0) continue;

            const auto& pos = decal.Position;
            const auto up = decal.Bitangent * decal.Radius;
            const auto right = decal.Tangent * decal.Radius;

            auto& material = Render::Materials->Get(decal.Texture);
            effect.Shader->SetDiffuse(ctx.CommandList(), material.Handles[0]);
            g_SpriteBatch->Begin(ctx.CommandList());

            ObjectVertex v0{ pos - up, { 0, 1 }, decal.Color };
            ObjectVertex v1{ pos - right, { 1, 1 }, decal.Color };
            ObjectVertex v2{ pos + up, { 1, 0 }, decal.Color };
            ObjectVertex v3{ pos + right, { 0, 0 }, decal.Color };
            g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
            g_SpriteBatch->End();
            Stats::DrawCalls++;
        }
    }

    void RemoveDecals(Tag tag) {
        if (!tag) return;
        auto cside = Game::Level.GetConnectedSide(tag);

        for (auto& decal : Decals) {
            Tag decalTag = { decal.Segment, decal.Side };
            if (decalTag == tag || (cside && decalTag == cside))
                decal.Life = 0;
        }
    }

    void SparkEmitter::FixedUpdate(float dt) {
        if (!_createdSparks) {
            // for now create all sparks when inserted. want to support random delay / permanent generators later.
            auto count = Count.GetRandom();
            for (uint i = 0; i < count; i++)
                CreateSpark();

            _createdSparks = true;
        }

        for (auto& spark : _sparks) {
            spark.Life -= dt;
            if (!Spark::IsAlive(spark)) continue;
            spark.PrevPosition = spark.Position;
            spark.PrevVelocity = spark.Velocity;

            spark.Velocity += Game::Gravity * dt;
            spark.Velocity *= 1 - Drag;
            spark.Position += spark.Velocity * dt;

            auto dir = spark.Velocity;
            dir.Normalize();

            Ray ray(spark.Position, dir);

            auto rayLen = Vector3::Distance(spark.PrevPosition, spark.Position) * 1.2f;
            LevelHit hit;
            bool hitSomething = IntersectLevel(Game::Level, ray, spark.Segment, rayLen, true, hit);

            if (!hitSomething) {
                // check surrounding segments
                auto& seg = Game::Level.GetSegment(spark.Segment);
                for (auto& side : SideIDs) {
                    hitSomething = IntersectLevel(Game::Level, ray, seg.GetConnection(side), rayLen, true, hit);
                    if (hitSomething)
                        break;
                }
            }

            if (hitSomething) {
                auto& side = Game::Level.GetSide(hit.Tag);
                auto& ti = Resources::GetLevelTextureInfo(side.TMap);
                if (ti.HasFlag(TextureFlag::Volatile) || ti.HasFlag(TextureFlag::Water)) {
                    // Remove sparks that hit a liquid
                    spark.Life = -1;
                    spark.Velocity *= 0.25f;
                    Sound3D sound(hit.Point, hit.Tag.Segment);
                    sound.Resource = Resources::GetSoundResource(SoundID::MissileHitWater);
                    sound.Volume = 0.6f;
                    sound.Radius = 75;
                    sound.Occlusion = false;
                    Sound::Play(sound);
                }
                else if (spark.Velocity.Length() > 0.5f) {
                    // skip low velocity particles to prevent jitter
                    // bounce sparks that hit a wall
                    spark.Velocity -= hit.Normal * hit.Normal.Dot(spark.Velocity) * (1 - Restitution);
                    spark.Velocity = Vector3::Reflect(spark.Velocity, hit.Normal);
                    spark.Segment = hit.Tag.Segment;
                }
            }
        }
    }

    void SparkEmitter::Draw(Graphics::GraphicsContext& ctx) {
        auto& effect = Effects->SpriteAdditive;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->FrameConstantsBuffer.GetGPUVirtualAddress());
        auto cmdList = ctx.CommandList();

        effect.Shader->SetSampler(cmdList, Heaps->States.AnisotropicClamp());
        auto& material = Render::Materials->Get(Texture);
        effect.Shader->SetDiffuse(ctx.CommandList(), material.Handles[0]);
        g_SpriteBatch->Begin(ctx.CommandList());

        for (auto& spark : _sparks) {
            if (spark.Life <= 0) continue;
            auto pos = Vector3::Lerp(spark.PrevPosition, spark.Position, Game::LerpAmount);
            auto vec = Vector3::Lerp(spark.PrevVelocity, spark.Velocity, Game::LerpAmount);
            vec.Normalize();
            Vector3 head = pos + vec * Width * 0.5;
            Vector3 tail = pos - vec * Width * 0.5;

            auto size = spark.Velocity * VelocitySmear;
            head += size;
            tail -= size;

            auto tangent = GetBeamNormal(head, tail) * Width * 0.5f;

            auto color = Color;
            if (FadeTime > 0)
                color.w = std::lerp(1.0f, 0.0f, std::clamp((FadeTime - spark.Life) / FadeTime, 0.0f, 1.0f));

            ObjectVertex v0{ head + tangent, { 0, 1 }, color };
            ObjectVertex v1{ head - tangent, { 1, 1 }, color };
            ObjectVertex v2{ tail - tangent, { 1, 0 }, color };
            ObjectVertex v3{ tail + tangent, { 0, 0 }, color };
            g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
        }

        g_SpriteBatch->End();
        Render::Stats::DrawCalls++;
    }

    void SparkEmitter::CreateSpark() {
        Spark spark;
        spark.Life = Duration.GetRandom();
        spark.Position = spark.PrevPosition = Position;

        if (Direction == Vector3::Zero) {
            spark.Velocity = RandomVector(Velocity.GetRandom());
        }
        else {
            auto spread = RandomPointInCircle(ConeRadius);
            auto direction = Direction;
            auto right = Direction.Cross(Up);
            direction += right * spread.x;
            direction += Up * spread.y;
            direction.Normalize();
            spark.Velocity = direction * Velocity.GetRandom();
            spark.Segment = Segment;
        }

        _sparks.Add(spark);
    }

    void AddSparkEmitter(SparkEmitter& emitter) {
        std::array tex = { emitter.Texture };
        Render::Materials->LoadTextures(tex);
        AddEffect(MakePtr<SparkEmitter>(emitter));
    }

    void ResetParticles() {
        ParticleEmitters.Clear();
        Beams.Clear();
        Explosions.Clear();

        for (auto& decal : Decals)
            decal.Life = 0;
    }

    void UpdateEffects(float dt) {
        UpdateExplosions(dt);

        for (auto& effects : SegmentEffects) {
            int i = 0;
            for (auto&& effect : effects) {
                if (effect && effect->IsAlive())
                    effect->Update(dt);
                i++;
            }
        }
    }

    void FixedUpdateEffects(float dt) {
        for (auto& effects : SegmentEffects) {
            int i = 0;
            for (auto&& effect : effects) {
                if (effect && effect->IsAlive())
                    effect->FixedUpdate(dt);
                i++;
            }
        }
    }

    void InitEffects(const Level& level) {
        SegmentEffects.clear();
        SegmentEffects.resize(level.Segments.size());
    }
}
