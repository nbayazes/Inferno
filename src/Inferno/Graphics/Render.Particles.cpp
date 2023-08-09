#include "pch.h"
#include "Render.Particles.h"
#include "DataPool.h"
#include "Render.h"
#include "Game.h"
#include "Game.Object.h"
#include "Game.Segment.h"
#include "MaterialLibrary.h"
#include "Physics.h"
#include "Render.Queue.h"
#include "SoundSystem.h"

namespace Inferno::Render {
    using Graphics::GraphicsContext;

    namespace {
        DataPool<BeamInfo> Beams(&BeamInfo::IsAlive, 50);
        Array<DecalInfo, 100> Decals;
        Array<DecalInfo, 20> AdditiveDecals;
        uint16 DecalIndex = 0;
        uint16 AdditiveDecalIndex = 0;

        DataPool<ExplosionInfo> Explosions(&ExplosionInfo::IsAlive, 50);
        DataPool<ParticleEmitter> ParticleEmitters(&ParticleEmitter::IsAlive, 10);
        List<List<Ptr<EffectBase>>> SegmentEffects; // equals segment count
    }

    span<Ptr<EffectBase>> GetEffectsInSegment(SegID id) {
        return SegmentEffects[(int)id];
    }

    void AddEffect(Ptr<EffectBase> e) {
        assert(e->Segment > SegID::None);
        e->IsAlive = true;
        auto seg = (int)e->Segment;

        for (auto& effect : SegmentEffects[seg]) {
            if (!effect || !effect->IsAlive) {
                effect = std::move(e);
                return;
            }
        }

        //SPDLOG_INFO("Add effect {}", SegmentEffects[seg].size());
        SegmentEffects[seg].push_back(std::move(e));
    }

    void AddParticle(Particle& p, SegID seg) {
        auto& vclip = Resources::GetVideoClip(p.Clip);
        if (vclip.NumFrames <= 0) return;
        p.Duration = vclip.PlayTime;
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

    // Returns the offset and submodel
    SubmodelRef GetRandomPointOnObject(const Object& obj) {
        if (obj.Render.Type == RenderType::Model) {
            auto& model = Resources::GetModel(obj.Render.Model.ID);
            auto sm = (short)RandomInt((int)model.Submodels.size() - 1);
            if (sm < 0) return { 0 };
            int index = -1;
            if (!model.Submodels[sm].Indices.empty()) {
                auto i = RandomInt((int)model.Submodels[sm].Indices.size() - 1);
                index = model.Submodels[sm].Indices[i];
            }
            else if (!model.Submodels[sm].FlatIndices.empty()) {
                auto i = RandomInt((int)model.Submodels[sm].FlatIndices.size() - 1);
                index = model.Submodels[sm].FlatIndices[i];
            }

            if (index < 0) return { 0 };
            Vector3 vert = model.Vertices[index];
            return { sm, vert };
        }
        else {
            auto point = obj.GetPosition(Game::LerpAmount) + RandomPointOnSphere() * obj.Radius;
            return { 0, point };
        }
    }

    bool Particle::Update(float dt) {
        if (!EffectBase::Update(dt)) return false;

        if (auto parent = Game::Level.TryGetObject(Parent)) {
            auto pos = parent->GetPosition(Game::LerpAmount);
            if (Submodel.ID > -1) {
                auto offset = GetSubmodelOffset(*parent, Submodel);
                pos += Vector3::Transform(offset, parent->GetRotation(Game::LerpAmount));
            }

            Position = pos;
        }

        return true;
    }

    void Particle::Draw(Graphics::GraphicsContext& ctx) {
        if (Delay > 0 || Elapsed > Duration) return;

        auto& vclip = Resources::GetVideoClip(Clip);

        auto* up = Up == Vector3::Zero ? nullptr : &Up;
        auto color = Color;
        float remaining = Duration - Elapsed;
        if (FadeTime != 0 && remaining <= FadeTime) {
            color.w = 1 - std::clamp((FadeTime - remaining) / FadeTime, 0.0f, 1.0f);
        }
        auto tid = vclip.GetFrame(Elapsed);
        DrawBillboard(ctx, tid, Position, Radius, color, true, Rotation, up);
    }

    Particle ParticleEmitterInfo::CreateParticle() const {
        auto& vclip = Resources::GetVideoClip(Clip);

        Particle p;
        p.Color = Color;
        p.Clip = Clip;
        p.Duration = vclip.PlayTime;
        p.Parent = Parent;
        //p.Submodel.Offset = ParentOffset;
        //p.Submodel.ID = 0;
        p.Position = Position;
        p.Radius = MinRadius + Random() * (MaxRadius - MinRadius);

        if (RandomRotation)
            p.Rotation = Random() * DirectX::XM_2PI;

        return p;
    }

    bool ParticleEmitter::Update(float dt) {
        if (!EffectBase::Update(dt)) return false;
        if (!IsAlive) return false;

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

        return true;
    }

    void Debris::Draw(Graphics::GraphicsContext& ctx) {
        auto& model = Resources::GetModel(Model);
        if (model.DataSize == 0) return;
        if (!Seq::inRange(model.Submodels, Submodel)) return;
        auto& meshHandle = GetMeshHandle(Model);

        auto& effect = Effects->Object;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        auto cmdList = ctx.GetCommandList();
        effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
        effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
        effect.Shader->SetMaterialInfoBuffer(cmdList, Render::MaterialInfoBuffer->GetSRV());
        effect.Shader->SetLightGrid(cmdList, *Render::LightGrid);

        effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
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

            //TexID tid = TexOverride;
            //if (tid == TexID::None)
            //    tid = mesh->EffectClip == EClipID::None ? mesh->Texture : Resources::GetEffectClip(mesh->EffectClip).VClip.GetFrame(ElapsedTime);

            //const Material2D& material = tid == TexID::None ? Materials->White : Materials->Get(tid);
            //effect.Shader->SetMaterial(cmdList, material);

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
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        auto cmdList = ctx.GetCommandList();

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
        Velocity += Game::Gravity * dt;
        Velocity *= 1 - Drag;
        Duration -= dt;
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
            .Radius = Radius
        };

        if (IntersectLevelDebris(Game::Level, capsule, Segment, hit)) {
            Elapsed = Duration; // destroy on contact
            // todo: scorch marks on walls
        }

        // todo: use cheaper way to update segments
        if (!PointInSegment(Game::Level, Segment, position)) {
            auto id = FindContainingSegment(Game::Level, position);
            if (id != SegID::None) Segment = id;
        }
    }

    void Debris::OnExpire() {
        ExplosionInfo e;
        e.Radius = { Radius * 2.0f, Radius * 2.45f };
        //e.Instances = 2;
        //e.Delay = { 0.15f, 0.3f };
        SPDLOG_INFO("Create debris explosion");
        CreateExplosion(e, Segment, PrevTransform.Translation());
    }

    void AddDebris(Debris& debris, SegID seg) {
        debris.Segment = seg;
        AddEffect(MakePtr<Debris>(debris));
    }

    void CreateExplosion(ExplosionInfo& e, SegID seg, const Vector3& position) {
        if (e.Clip == VClipID::None) return;
        if (e.InitialDelay < 0) e.InitialDelay = 0;
        if (e.Instances < 0) e.Instances = 1;
        e.Segment = seg;
        e.Position = position;
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
                sound.Volume = expl.Volume;
                Sound::Play(sound);
                //sound.Source = expl.Parent; // no parent so all nearby sounds merge
            }

            for (int i = 0; i < expl.Instances; i++) {
                Render::Particle p{};
                p.Position = expl.Position;
                if (expl.Variance > 0)
                    p.Position += Vector3(RandomN11() * expl.Variance, RandomN11() * expl.Variance, RandomN11() * expl.Variance);

                p.Radius = expl.Radius.GetRandom();
                p.Clip = expl.Clip;
                p.Color = expl.Color;
                p.FadeTime = expl.FadeTime;
                p.LightColor = expl.LightColor;
                // only apply light to first explosion instance
                if (i == 0) p.LightRadius = expl.LightRadius < 0 ? expl.LightRadius : p.Radius * 4;

                Render::AddParticle(p, expl.Segment);

                if (expl.Instances > 1 && (expl.Delay.Min > 0 || expl.Delay.Max > 0)) {
                    expl.InitialDelay = expl.Delay.GetRandom();
                    expl.Instances--;
                    break;
                }
            }
        }
    }

    // gets a random point at a given radius, intersecting the level
    Vector3 GetRandomPoint(const Vector3& pos, SegID seg, float radius) {
        //Vector3 end;
        LevelHit hit;
        auto dir = RandomVector(1);
        dir.Normalize();

        if (IntersectRayLevel(Game::Level, { pos, dir }, seg, radius, false, true, hit))
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

    void InitRandomBeamPoints(BeamInfo& beam, const Object* object) {
        if (HasFlag(beam.Flags, BeamFlag::RandomObjStart)) {
            if (object)
                beam.StartSubmodel = GetRandomPointOnObject(*object);
        }

        if (HasFlag(beam.Flags, BeamFlag::RandomObjEnd)) {
            if (object)
                beam.EndSubmodel = GetRandomPointOnObject(*object);
        }
        else if (HasFlag(beam.Flags, BeamFlag::RandomEnd)) {
            beam.End = GetRandomPoint(beam.Start, beam.Segment, beam.Radius.GetRandom());
        }
    }

    void AddBeam(BeamInfo& beam) {
        beam.Segment = FindContainingSegment(Game::Level, beam.Start);
        std::array tex = { beam.Texture };
        Render::Materials->LoadTextures(tex);

        if (beam.HasRandomEndpoints())
            InitRandomBeamPoints(beam, Game::Level.TryGetObject(beam.StartObj));

        beam.Runtime.Length = (beam.Start - beam.End).Length();
        beam.Runtime.Width = beam.Width.GetRandom();
        beam.Runtime.OffsetU = Random();
        Beams.Add(beam);
    }

    void AddBeam(BeamInfo beam, float life, const Vector3& start, const Vector3& end) {
        beam.Segment = FindContainingSegment(Game::Level, start);
        beam.Start = start;
        beam.End = end;
        beam.StartLife = beam.Life = life;
        AddBeam(beam);
    }

    void AddBeam(BeamInfo beam, float life, ObjID start, const Vector3& end, int startGun) {
        auto obj = Game::Level.TryGetObject(start);

        if (obj) {
            beam.StartObj = start;
            if (startGun >= 0) {
                beam.Start = GetGunpointOffset(*obj, (uint8)startGun);
                beam.StartSubmodel = GetLocalGunpointOffset(*obj, (uint8)startGun);
            }
            else {
                beam.Start = obj->Position;
            }
            beam.Segment = obj->Segment;
            beam.End = end;
            beam.StartLife = beam.Life = life;
            AddBeam(beam);
        }
    }

    void AddBeam(BeamInfo beam, float life, ObjID start, ObjID end, int startGun) {
        auto obj = Game::Level.TryGetObject(start);

        if (obj) {
            beam.StartObj = start;
            if (startGun >= 0) {
                beam.Start = GetGunpointOffset(*obj, (uint8)startGun);
                beam.StartSubmodel = GetLocalGunpointOffset(*obj, (uint8)startGun);
            }
            else {
                beam.Start = obj->Position;
            }
            beam.Segment = obj->Segment;
            beam.EndObj = end;
            beam.StartLife = beam.Life = life;
            AddBeam(beam);
        }
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
    }

    // Fractal noise generator, power of 2 wavelength
    void FractalNoise(span<float> noise) {
        if (noise.size() < 2) return;
        int div2 = (int)noise.size() >> 1;

        // noise is normalized to +/- scale
        noise[div2] = (noise.front() + noise.back()) * 0.5f + noise.size() * RandomN11() * 0.125f;

        if (div2 > 1) {
            FractalNoise(noise.subspan(0, div2 + 1));
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
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        effect.Shader->SetDepthTexture(ctx.GetCommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
        effect.Shader->SetSampler(ctx.GetCommandList(), Render::GetWrappedTextureSampler());

        for (auto& beam : Beams) {
            if (beam.StartDelay > 0) {
                beam.StartDelay -= Render::FrameTime;
                continue;
            }
            beam.Life -= Render::FrameTime;

            if (!beam.IsAlive()) continue;

            Object* startObj = nullptr;
            Object* endObj = nullptr;
            if (beam.StartObj != ObjID::None) startObj = Game::Level.TryGetObject(beam.StartObj);
            if (beam.EndObj != ObjID::None) endObj = Game::Level.TryGetObject(beam.EndObj);

            if (beam.StartObj != ObjID::None && !HasFlag(beam.Flags, BeamFlag::RandomObjStart)) {
                if (startObj) {
                    if (beam.StartSubmodel.ID > -1) {
                        auto offset = GetSubmodelOffset(*startObj, beam.StartSubmodel);
                        beam.Start = Vector3::Transform(offset, startObj->GetTransform(Game::LerpAmount));
                    }
                    else {
                        beam.Start = startObj->GetPosition(Game::LerpAmount);
                    }
                }
            }

            if (beam.HasRandomEndpoints() && Render::ElapsedTime > beam.Runtime.NextStrikeTime) {
                InitRandomBeamPoints(beam, startObj);
                beam.Runtime.NextStrikeTime = Render::ElapsedTime + beam.StrikeTime;
            }

            if (HasFlag(beam.Flags, BeamFlag::RandomObjStart) && startObj) {
                auto offset = GetSubmodelOffset(*startObj, beam.StartSubmodel);
                beam.Start = Vector3::Transform(offset, startObj->GetTransform(Game::LerpAmount));
            }

            if (HasFlag(beam.Flags, BeamFlag::RandomObjEnd) && startObj) {
                // note that this effect uses the start object for begin and end
                auto offset = GetSubmodelOffset(*startObj, beam.EndSubmodel);
                beam.End = Vector3::Transform(offset, startObj->GetTransform(Game::LerpAmount));
            }
            else if (endObj) {
                beam.End = endObj->GetPosition(Game::LerpAmount);
            }

            beam.Time += Render::FrameTime;
            auto& noise = beam.Runtime.Noise;
            auto delta = beam.End - beam.Start;
            auto length = delta.Length();
            if (length < 1) continue; // don't draw really short beams

            // DrawSegs()
            auto scale = beam.Amplitude;

            int segments = (int)(length / (beam.Runtime.Width * 0.5 * 1.414)) + 1;
            segments = std::clamp(segments, 2, 64);
            auto div = 1.0f / (segments - 1);

            auto vLast = std::fmodf(beam.Time * beam.ScrollSpeed, 1);
            if (HasFlag(beam.Flags, BeamFlag::SineNoise)) {
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
                if (HasFlag(beam.Flags, BeamFlag::SineNoise))
                    SineNoise(noise);
                else
                    FractalNoise(noise);

                beam.Runtime.NextUpdate = Render::ElapsedTime + beam.Frequency;
                beam.Runtime.OffsetU = Random();
            }

            struct BeamSeg {
                Vector3 pos;
                float texcoord;
                Color color;
            };

            BeamSeg curSeg{};
            auto vStep = length / 20 * div * beam.Scale;

            auto& material = Render::Materials->Get(beam.Texture);
            effect.Shader->SetDiffuse(ctx.GetCommandList(), material.Handle());
            Stats::DrawCalls++;
            g_SpriteBatch->Begin(ctx.GetCommandList());

            Vector3 prevNormal;
            Vector3 prevUp;

            auto tangent = GetBeamNormal(beam.Start, beam.End);

            float fade = 1;
            if (beam.FadeInOutTime > 0) {
                auto elapsedLife = beam.StartLife - beam.Life;
                if (elapsedLife < beam.FadeInOutTime) {
                    fade = 1 - (beam.FadeInOutTime - elapsedLife) / beam.FadeInOutTime;
                }
                else if (beam.Life < beam.FadeInOutTime) {
                    fade = 1 - (beam.FadeInOutTime - beam.Life) / beam.FadeInOutTime;
                }
            }

            for (int i = 0; i < segments; i++) {
                BeamSeg nextSeg{ .color = beam.Color };
                auto fraction = i * div;

                nextSeg.pos = beam.Start + delta * fraction;

                if (beam.Amplitude != 0) {
                    //auto factor = beam.Runtime.Noise[noiseIndex >> 16] * beam.Amplitude;
                    auto factor = noise[i] * beam.Amplitude;

                    if (HasFlag(beam.Flags, BeamFlag::SineNoise)) {
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

                nextSeg.texcoord = beam.Runtime.OffsetU + vLast;
                float brightness = HasFlag(beam.Flags, BeamFlag::FadeStart) ? 0.0f : 1.0f;
                if (HasFlag(beam.Flags, BeamFlag::FadeStart) && HasFlag(beam.Flags, BeamFlag::FadeEnd)) {
                    if (fraction < 0.5f)
                        brightness = 2.0f * fraction;
                    else
                        brightness = 2.0f * (1.0f - fraction);
                }
                else if (HasFlag(beam.Flags, BeamFlag::FadeStart)) {
                    brightness = fraction;
                }
                else if (HasFlag(beam.Flags, BeamFlag::FadeEnd)) {
                    brightness = 1 - fraction;
                }

                brightness = std::clamp(brightness, 0.0f, 1.0f);
                nextSeg.color *= brightness;

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
                    auto up = avgNormal * beam.Runtime.Width * 0.5f;
                    if (i == 1) prevUp = up;

                    ObjectVertex v0{ start + prevUp, { 0, curSeg.texcoord }, curSeg.color * fade };
                    ObjectVertex v1{ start - prevUp, { 1, curSeg.texcoord }, curSeg.color * fade };
                    ObjectVertex v2{ end - up, { 1, nextSeg.texcoord }, nextSeg.color * fade };
                    ObjectVertex v3{ end + up, { 0, nextSeg.texcoord }, nextSeg.color * fade };

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

    bool TracerInfo::Update(float dt) {
        if (!EffectBase::Update(dt)) return false;
        auto parentWasLive = ParentIsLive;

        const auto obj = Game::Level.TryGetObject(Parent);

        if (obj && obj->Signature == Signature) {
            ParentIsLive = obj->IsAlive();
            End = obj->Position;
            if (ParentIsLive)
                Elapsed = 0; // Reset life
        }
        else {
            ParentIsLive = false;
        }

        parentWasLive = parentWasLive && !ParentIsLive;
        if (parentWasLive)
            Elapsed = Duration - FadeSpeed; // Start fading out the tracer if parent dies

        return true;
    }

    void TracerInfo::Draw(Graphics::GraphicsContext& ctx) {
        auto& effect = Effects->SpriteAdditive;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        effect.Shader->SetDepthTexture(ctx.GetCommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
        effect.Shader->SetSampler(ctx.GetCommandList(), Render::GetWrappedTextureSampler());

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
            effect.Shader->SetDiffuse(ctx.GetCommandList(), material.Handle());
            g_SpriteBatch->Begin(ctx.GetCommandList());

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
            effect.Shader->SetDiffuse(ctx.GetCommandList(), material.Handle());
            g_SpriteBatch->Begin(ctx.GetCommandList());

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

    void AddTracer(TracerInfo& tracer, SegID seg, ObjID parent) {
        std::array tex = { tracer.Texture, tracer.BlobTexture };
        Render::Materials->LoadTextures(tex);
        tracer.Segment = seg;
        tracer.Parent = parent;

        assert(tracer.Parent != ObjID::None);

        if (auto obj = Game::Level.TryGetObject(tracer.Parent)) {
            tracer.Position = obj->Position;
            tracer.Signature = obj->Signature;
        }
        else {
            SPDLOG_WARN("Tried to add tracer to invalid object");
            return;
        }

        tracer.Elapsed = 0;
        tracer.Duration = 5;
        AddEffect(MakePtr<TracerInfo>(tracer));
    }

    void AddDecal(DecalInfo& decal) {
        if (!Render::Materials->LoadTexture(decal.Texture))
            return;

        if (decal.Duration == 0)
            decal.Duration = FLT_MAX;

        if (decal.Additive) {
            AdditiveDecals[AdditiveDecalIndex++] = decal;

            if (AdditiveDecalIndex >= AdditiveDecals.size())
                AdditiveDecalIndex = 0;
        }
        else {
            Decals[DecalIndex++] = decal;

            if (DecalIndex >= Decals.size())
                DecalIndex = 0;
        }
    }

    void DrawDecal(const DecalInfo& decal, DirectX::PrimitiveBatch<ObjectVertex>& batch) {
        auto radius = decal.Radius;
        auto color = decal.Color;
        if (decal.FadeTime > 0) {
            float remaining = decal.Duration - decal.Elapsed;
            auto t = std::lerp(1.0f, 0.0f, std::clamp((decal.FadeTime - remaining) / decal.FadeTime, 0.0f, 1.0f));
            color.w = t;
            radius += (1 - t) * decal.Radius * 0.5f; // expand as fading out
        }

        const auto& pos = decal.Position;
        const auto up = decal.Bitangent * radius;
        const auto right = decal.Tangent * radius;

        ObjectVertex v0{ pos - up, { 0, 1 }, color };
        ObjectVertex v1{ pos - right, { 1, 1 }, color };
        ObjectVertex v2{ pos + up, { 1, 0 }, color };
        ObjectVertex v3{ pos + right, { 0, 0 }, color };
        batch.DrawQuad(v0, v1, v2, v3);
    }

    void DrawDecals(Graphics::GraphicsContext& ctx, float dt) {
        {
            auto& effect = Effects->SpriteMultiply;
            ctx.ApplyEffect(effect);
            ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
            effect.Shader->SetDepthTexture(ctx.GetCommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
            effect.Shader->SetSampler(ctx.GetCommandList(), Render::GetWrappedTextureSampler());

            for (auto& decal : Decals) {
                if (!decal.Update(dt)) continue;
                if (!decal.IsAlive) continue;

                auto& material = Render::Materials->Get(decal.Texture);
                effect.Shader->SetDiffuse(ctx.GetCommandList(), material.Handle());
                g_SpriteBatch->Begin(ctx.GetCommandList());
                DrawDecal(decal, *g_SpriteBatch.get());
                g_SpriteBatch->End();
                Stats::DrawCalls++;
            }
        }

        {
            auto& effect = Effects->SpriteAdditiveBiased;
            ctx.ApplyEffect(effect);
            ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
            effect.Shader->SetDepthTexture(ctx.GetCommandList(), Adapter->LinearizedDepthBuffer.GetSRV());
            effect.Shader->SetSampler(ctx.GetCommandList(), Render::GetWrappedTextureSampler());

            for (auto& decal : AdditiveDecals) {
                if (!decal.Update(dt)) continue;
                if (!decal.IsAlive) continue;

                auto& material = Render::Materials->Get(decal.Texture);
                effect.Shader->SetDiffuse(ctx.GetCommandList(), material.Handle());
                g_SpriteBatch->Begin(ctx.GetCommandList());
                DrawDecal(decal, *g_SpriteBatch.get());
                g_SpriteBatch->End();
                Stats::DrawCalls++;
            }
        }
    }

    span<DecalInfo> GetAdditiveDecals() { return AdditiveDecals; }
    span<DecalInfo> GetDecals() { return Decals; }

    void RemoveDecals(Tag tag) {
        if (!tag) return;
        auto cside = Game::Level.GetConnectedSide(tag);

        for (auto& decal : Decals) {
            Tag decalTag = { decal.Segment, decal.Side };
            if (decalTag == tag || (cside && decalTag == cside))
                decal.Elapsed = FLT_MAX;
        }
    }

    void RemoveEffects(ObjID id) {
        for (auto& beam : Beams) {
            if (beam.StartObj == id)
                beam.Life = 0;
        }

        if (auto obj = Game::Level.TryGetObject(id)) {
            for (auto& effect : SegmentEffects[(int)obj->Segment]) {
                if (effect->Parent == id)
                    effect->Elapsed = effect->Duration; // expire the effect
            }
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
            if (!spark.IsAlive()) continue;
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
            bool hitSomething = IntersectRayLevel(Game::Level, ray, spark.Segment, rayLen, true, true, hit);

            if (!hitSomething) {
                // check surrounding segments
                auto& seg = Game::Level.GetSegment(spark.Segment);
                for (auto& side : SideIDs) {
                    hitSomething = IntersectRayLevel(Game::Level, ray, seg.GetConnection(side), rayLen, true, true, hit);
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
                    //Sound3D sound(hit.Point, hit.Tag.Segment);
                    //sound.Resource = Resources::GetSoundResource(SoundID::MissileHitWater);
                    //sound.Volume = 0.6f;
                    //sound.Radius = 75;
                    //sound.Occlusion = false;
                    //Sound::Play(sound);
                }
                else {
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
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        auto cmdList = ctx.GetCommandList();

        effect.Shader->SetSampler(cmdList, Render::GetClampedTextureSampler());
        auto& material = Render::Materials->Get(Texture);
        effect.Shader->SetDiffuse(ctx.GetCommandList(), material.Handle());
        g_SpriteBatch->Begin(ctx.GetCommandList());

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
        spark.Life = SparkDuration.GetRandom();
        spark.Position = spark.PrevPosition = Position;
        spark.Segment = Segment;

        if (Direction == Vector3::Zero) {
            spark.Velocity = RandomVector(Velocity.GetRandom());
        }
        else {
            auto spread = RandomPointOnHemisphere();
            auto right = Direction.Cross(Up);
            //auto direction = Direction;
            Vector3 direction;
            direction += right * spread.x * ConeRadius;
            direction += Up * spread.y * ConeRadius;
            direction += Direction * spread.z;
            spark.Velocity = direction * Velocity.GetRandom();
        }

        _sparks.Add(spark);
    }

    //void AddSparkEmitter(SparkEmitter& emitter) {
    //    std::array tex = { emitter.Texture };
    //    Render::Materials->LoadTextures(tex);
    //    assert(emitter.Segment != SegID::None);
    //    if (emitter.Duration == 0) emitter.Duration = emitter.DurationRange.Max;
    //    AddEffect(MakePtr<SparkEmitter>(emitter));
    //}

    void AddSparkEmitter(SparkEmitter& emitter, SegID seg, const Vector3& position) {
        emitter.Segment = seg;
        emitter.Position = position;
        emitter.Color *= emitter.Color.w;
        emitter.Color.w = 0;

        Render::Materials->LoadTexture(emitter.Texture);
        assert(emitter.Segment != SegID::None);
        if (emitter.Duration == 0) emitter.Duration = emitter.SparkDuration.Max;
        AddEffect(MakePtr<SparkEmitter>(emitter));
    }

    void ResetParticles() {
        ParticleEmitters.Clear();
        Beams.Clear();
        Explosions.Clear();

        for (auto& decal : Decals)
            decal.Duration = 0;
    }

    void UpdateEffects(float dt) {
        UpdateExplosions(dt); // Explosions generate sprites that are added as segment effects

        for (auto& effects : SegmentEffects) {
            for (auto&& effect : effects) {
                if (effect && effect->IsAlive) 
                    effect->Update(dt);
            }

            // Do a second pass to expire effects in case other effects add new ones mid-frame
            for (auto&& effect : effects) {
                if (effect->IsAlive && effect->Elapsed >= effect->Duration) {
                    effect->IsAlive = false;
                    effect->OnExpire();
                }
            }
        }
    }

    void FixedUpdateEffects(float dt) {
        for (auto& effects : SegmentEffects) {
            for (auto&& effect : effects) {
                if (effect && effect->IsAlive)
                    effect->FixedUpdate(dt);
            }
        }
    }

    void InitEffects(const Level& level) {
        SegmentEffects.clear();
        SegmentEffects.resize(level.Segments.size());
    }
}
