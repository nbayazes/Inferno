#include "pch.h"
#include <pix3.h>
#include "Render.Particles.h"
#include "DataPool.h"
#include "Editor/UI/TextureEditor.h"
#include "Render.h"
#include "Game.h"
#include "Game.Object.h"
#include "Game.Segment.h"
#include "LegitProfiler.h"
#include "MaterialLibrary.h"
#include "OpenSimplex2.h"
#include "Physics.h"
#include "Render.Queue.h"
#include "SoundSystem.h"

namespace Inferno::Render {
    namespace {
        Array<DecalInfo, 100> Decals;
        Array<DecalInfo, 20> AdditiveDecals;
        uint16 DecalIndex = 0;
        uint16 AdditiveDecalIndex = 0;

        List<Ptr<EffectBase>> VisualEffects;
    }

    struct LightEffect final : EffectBase {
        explicit LightEffect(const LightEffectInfo& info) : Info(info) { Queue = RenderQueueType::None; }
        LightEffectInfo Info;

        void OnUpdate(float dt, EffectID) override;

    private:
        Color _currentColor;
        float _currentRadius = 0;
    };

    struct Particle final : EffectBase {
        explicit Particle(const ParticleInfo& info) : Info(info) {}
        ParticleInfo Info;
        //float FadeDuration = 0;

        void Draw(GraphicsContext&) override;
    };

    struct ParticleEmitterInfo {
        VClipID Clip = VClipID::None;
        float Life = 0; // How long the emitter lives for
        ObjRef Parent; // Moves with this object
        //Vector3 ParentOffset; // Offset from parent
        Vector3 Position;
        Vector3 Velocity;

        //SegID Segment = SegID::None; // For sorting
        Color Color = { 1, 1, 1 };
        float Variance = 0;
        bool RandomRotation = true;
        int ParticlesToSpawn = 1; // stops creating particles once this reaches zero. -1 to create particles forever
        float StartDelay = 0; // How long to wait before emitting particles
        float MinDelay = 0, MaxDelay = 0; // How often to spawn a particle
        float MinRadius = 1, MaxRadius = 2;

        Particle CreateParticle() const;
    };

    class ParticleEmitter final : public EffectBase {
        float _spawnTimer = 0; // internal timer for when to create a particle
        ParticleEmitterInfo _info;
        DataPool<Particle> _particles;

    public:
        explicit ParticleEmitter(const ParticleEmitterInfo& info, size_t capacity)
            : _info(info), _particles(IsExpired, capacity) {
            StartDelay = info.StartDelay;
            Position = info.Position;
        }

        void OnUpdate(float dt, EffectID) override;
    };

    // Remains of a destroyed robot
    struct Debris final : EffectBase {
        explicit Debris(const DebrisInfo& info) : Info(info) { Queue = RenderQueueType::Opaque; }

        Matrix Transform, PrevTransform;
        Vector3 Velocity;
        Vector3 AngularVelocity;
        DebrisInfo Info;

        void Draw(GraphicsContext&) override;
        void DepthPrepass(GraphicsContext&) override;
        void OnFixedUpdate(float dt, EffectID) override;
        void OnExpire() override;
    };

    // An explosion can consist of multiple particles
    struct ExplosionEffect : EffectBase {
        ExplosionEffect() { Queue = RenderQueueType::None; }

        ExplosionEffectInfo Info;

        //bool IsAlive() const { return InitialDelay >= 0; }
        void OnUpdate(float, EffectID) override;
    };


    bool IsExpired(const EffectBase& effect) {
        return Game::Time >= effect.CreationTime + effect.Duration;
    }

    float EffectBase::GetRemainingTime() const {
        return std::max(0.0f, float(Duration + CreationTime - Game::Time));
    }

    float EffectBase::GetElapsedTime() const {
        return Duration - GetRemainingTime();
    }

    EffectBase* GetEffect(EffectID effect) {
        if (!Seq::inRange(VisualEffects, (int)effect)) return nullptr;
        return VisualEffects[(int)effect].get();
    }

    // Links an effect to a segment, removing it from the existing one if necessary
    void LinkEffect(EffectBase& effect, EffectID id, SegID segId) {
        if (effect.Segment == segId) return;

        // Remove from old segment
        if (auto existing = Game::Level.TryGetSegment(effect.Segment)) {
            //assert(Seq::contains(existing->Effects, id));
            Seq::remove(existing->Effects, id);
        }

        if (segId == SegID::None) return;

        // Add to new segment
        if (auto seg = Game::Level.TryGetSegment(segId)) {
            //ASSERT(!Seq::contains(seg->Effects, id));
            if (!Seq::contains(seg->Effects, id)) {
                //SPDLOG_INFO("Moving effect {} from seg {} to seg {}", (int)id, effect.Segment, segId);
                seg->Effects.push_back(id);
                effect.Segment = segId;
            }
        }
    }

    void UnlinkEffect(EffectBase& effect, EffectID id) {
        LinkEffect(effect, id, SegID::None);
    }

    EffectID AddEffect(Ptr<EffectBase> e) {
        if (VisualEffects.size() >= VisualEffects.capacity()) {
            //__debugbreak(); // Cannot resize effects array mid frame!
            return EffectID::None;
        }

        ASSERT(e->Segment > SegID::None); // Caller forgot to set segment
        if (!Seq::inRange(Game::Level.Segments, (int)e->Segment))
            return EffectID::None;

        e->CreationTime = Game::Time;
        e->UpdatePositionFromParent();
        e->OnInit();

        auto& seg = Game::Level.GetSegment(e->Segment);
        auto newId = EffectID::None;

        // Find an unused effect slot
        for (size_t i = 0; i < VisualEffects.size(); i++) {
            auto& effect = VisualEffects[i];
            if (!effect) {
                effect = std::move(e);
                newId = EffectID(i);
                break;
            }
        }

        // Add a new slot
        if (newId == EffectID::None) {
            newId = EffectID(VisualEffects.size());
            VisualEffects.push_back(std::move(e));
        }

        ASSERT(newId != EffectID::None);
        ASSERT(!Seq::contains(seg.Effects, newId));
        seg.Effects.push_back(newId);
        return newId;
        //SPDLOG_INFO("Add effect {}", SegmentEffects[seg].size());
    }

    void AddBeamInstance(BeamInstance& beam) {
        auto segId = FindContainingSegment(Game::Level, beam.Start);
        if (segId != SegID::None) beam.Segment = segId;

        std::array tex = { beam.Info.Texture };
        Render::Materials->LoadTextures(tex);

        if (beam.Info.HasRandomEndpoints())
            beam.InitRandomPoints(Game::GetObject(beam.Parent));

        beam.Runtime.Length = (beam.Start - beam.End).Length();
        beam.Runtime.Width = beam.Info.Width.GetRandom();
        beam.Runtime.OffsetU = Random();
        AddEffect(MakePtr<BeamInstance>(beam));
    }

    void AddBeam(const BeamInfo& info, SegID seg, float duration, const Vector3& start, const Vector3& end) {
        BeamInstance beam;
        beam.Info = info;
        beam.Start = start;
        beam.End = end;
        beam.Segment = seg;
        if (duration > 0) beam.Duration = duration;
        AddBeamInstance(beam);
    }

    void AddBeam(const BeamInfo& info, float duration, ObjRef start, const Vector3& end, int startGun) {
        if (auto obj = Game::GetObject(start)) {
            BeamInstance beam;
            beam.Info = info;
            beam.Parent = start;

            if (startGun >= 0) {
                beam.Start = GetGunpointOffset(*obj, (uint8)startGun);
                beam.ParentSubmodel = GetGunpointSubmodelOffset(*obj, (uint8)startGun);
            }
            else {
                beam.Start = obj->Position;
            }

            beam.Segment = obj->Segment;
            beam.End = end;
            if (duration > 0) beam.Duration = duration;
            AddBeamInstance(beam);
        }
    }

    void AttachBeam(const BeamInfo& info, float duration, ObjRef start, ObjRef end, int startGun) {
        if (auto obj = Game::GetObject(start)) {
            BeamInstance beam;
            beam.Info = info;
            beam.Parent = start;

            if (startGun >= 0) {
                beam.Start = GetGunpointOffset(*obj, (uint8)startGun);
                beam.ParentSubmodel = GetGunpointSubmodelOffset(*obj, (uint8)startGun);
            }
            else {
                beam.Start = obj->Position;
            }

            beam.Segment = obj->Segment;
            beam.EndObj = end;
            if (duration > 0) beam.Duration = duration;
            AddBeamInstance(beam);
        }
    }

    void AddParticle(const ParticleInfo& info, SegID seg, const Vector3& position) {
        auto& vclip = Resources::GetVideoClip(info.Clip);
        if (vclip.NumFrames <= 0) return;

        Particle p(info);
        p.Duration = vclip.PlayTime;
        p.Segment = seg;
        p.Position = position;
        if (info.RandomRotation)
            p.Info.Rotation = Random() * DirectX::XM_2PI;

        Render::LoadTextureDynamic(info.Clip);
        AddEffect(MakePtr<Particle>(p));
    }

    void AttachParticle(const ParticleInfo& info, ObjRef parent, SubmodelRef submodel) {
        auto obj = Game::GetObject(parent);
        if (!obj) return;

        auto& vclip = Resources::GetVideoClip(info.Clip);
        if (vclip.NumFrames <= 0) return;

        Particle p(info);
        p.Duration = vclip.PlayTime;
        p.Segment = obj->Segment;
        p.Position = obj->GetPosition(Game::LerpAmount);
        p.Parent = parent;
        p.ParentSubmodel = submodel;

        if (info.RandomRotation)
            p.Info.Rotation = Random() * DirectX::XM_2PI;

        Render::LoadTextureDynamic(info.Clip);
        AddEffect(MakePtr<Particle>(p));
    }

    void Particle::Draw(GraphicsContext& ctx) {
        if (Info.Delay > 0 || IsExpired(*this)) return;

        auto& vclip = Resources::GetVideoClip(Info.Clip);

        auto* up = Info.Up == Vector3::Zero ? nullptr : &Info.Up;
        auto color = Info.Color;
        float remaining = GetRemainingTime();
        if (FadeTime != 0 && remaining <= FadeTime) {
            color.w = 1 - std::clamp((FadeTime - remaining) / FadeTime, 0.0f, 1.0f);
        }

        auto tid = vclip.GetFrameClamped(GetElapsedTime());
        DrawBillboard(ctx, tid, Position, Info.Radius, color, true, Info.Rotation, up);
    }

    Particle ParticleEmitterInfo::CreateParticle() const {
        auto& vclip = Resources::GetVideoClip(Clip);

        ParticleInfo info;
        info.Color = Color;
        info.Clip = Clip;
        //p.Submodel.Offset = ParentOffset;
        //p.Submodel.ID = 0;
        info.Radius = MinRadius + Random() * (MaxRadius - MinRadius);

        if (RandomRotation)
            info.Rotation = Random() * DirectX::XM_2PI;

        Particle p(info);
        p.Position = Position;
        p.Parent = Parent;
        p.Duration = vclip.PlayTime;
        return p;
    }

    void ParticleEmitter::OnUpdate(float dt, EffectID) {
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

    void Debris::Draw(GraphicsContext& ctx) {
        auto& model = Resources::GetModel(Info.Model);
        if (model.DataSize == 0) return;
        if (!Seq::inRange(model.Submodels, Info.Submodel)) return;
        auto& meshHandle = GetMeshHandle(Info.Model);

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
        constants.TexIdOverride = (int)Info.TexOverride;

        Matrix transform = Matrix::Lerp(PrevTransform, Transform, Game::LerpAmount);
        //transform.Forward(-transform.Forward()); // flip z axis to correct for LH models
        constants.World = transform;
        effect.Shader->SetConstants(cmdList, constants);

        // get the mesh associated with the submodel
        auto& subMesh = meshHandle.Meshes[Info.Submodel];

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

    void Debris::DepthPrepass(GraphicsContext& ctx) {
        auto& model = Resources::GetModel(Info.Model);
        if (model.DataSize == 0) return;
        if (!Seq::inRange(model.Submodels, Info.Submodel)) return;

        auto& meshHandle = GetMeshHandle(Info.Model);
        auto cmdList = ctx.GetCommandList();
        auto& effect = Effects->DepthObject;
        if (ctx.ApplyEffect(effect)) {
            ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
            effect.Shader->SetSampler(cmdList, GetWrappedTextureSampler());
            effect.Shader->SetTextureTable(cmdList, Render::Heaps->Materials.GetGpuHandle(0));
            effect.Shader->SetVClipTable(cmdList, Render::VClipBuffer->GetSRV());
        }

        Matrix transform = Matrix::Lerp(PrevTransform, Transform, Game::LerpAmount);
        //transform.Forward(-transform.Forward()); // flip z axis to correct for LH models

        ObjectDepthShader::Constants constants = {};
        constants.World = transform;

        effect.Shader->SetConstants(cmdList, constants);

        // get the mesh associated with the submodel
        auto& subMesh = meshHandle.Meshes[Info.Submodel];

        for (int i = 0; i < subMesh.size(); i++) {
            auto mesh = subMesh[i];
            if (!mesh) continue;

            cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBuffer);
            cmdList->IASetIndexBuffer(&mesh->IndexBuffer);
            cmdList->DrawIndexedInstanced(mesh->IndexCount, 1, 0, 0, 0);
            Stats::DrawCalls++;
        }
    }

    void Debris::OnFixedUpdate(float dt, EffectID effectId) {
        Velocity += Game::Gravity * dt;
        Velocity *= 1 - Info.Drag;
        PrevTransform = Transform;
        auto position = Transform.Translation() + Velocity * dt;
        //Transform.Translation(Transform.Translation() + Velocity * dt);

        const auto drag = Info.Drag * 5 / 2;
        AngularVelocity *= 1 - drag;
        Transform.Translation(Vector3::Zero);
        Transform = Matrix::CreateFromYawPitchRoll(-AngularVelocity * dt * DirectX::XM_2PI) * Transform;
        Transform.Translation(position);

        LevelHit hit;
        DirectX::BoundingSphere sphere{ Transform.Translation(), Info.Radius };

        if (IntersectLevelDebris(Game::Level, sphere, Segment, hit)) {
            Duration = 0; // destroy on contact
            // todo: scorch marks on walls
        }

        if (!PointInSegment(Game::Level, Segment, position)) {
            LinkEffect(*this, effectId, Segment);
        }
    }

    void Debris::OnExpire() {
        //ExplosionEffect e;
        //e.Info.Radius = { Info.Radius * 2.0f, Info.Radius * 2.5f };

        ExplosionEffectInfo info;
        info.Radius = { Info.Radius * 2.0f, Info.Radius * 2.5f };

        //e.Instances = 2;
        //e.Delay = { 0.15f, 0.3f };
        //SPDLOG_INFO("Create debris explosion");
        CreateExplosion(info, Segment, PrevTransform.Translation());
    }

    void AddDebris(const DebrisInfo& info, const Matrix& transform, SegID seg, const Vector3& velocity, const Vector3& angularVelocity, float duration) {
        Debris debris(info);
        debris.Segment = seg;
        debris.Velocity = velocity;
        debris.AngularVelocity = angularVelocity;
        debris.Duration = duration;
        debris.Transform = debris.PrevTransform = transform;
        AddEffect(MakePtr<Debris>(debris));
    }

    void CreateExplosion(ExplosionEffectInfo& info, SegID seg, const Vector3& position, float duration, float startDelay) {
        if (info.Clip == VClipID::None) return;
        if (info.Instances < 0) info.Instances = 1;
        if (duration <= 0) duration = startDelay + info.Delay.Max * info.Instances;

        ExplosionEffect e;
        e.Segment = seg;
        e.Position = position;
        e.StartDelay = startDelay;
        e.Duration = duration;
        AddEffect(MakePtr<ExplosionEffect>(e));
    }

    void CreateExplosion(ExplosionEffectInfo& info, ObjRef parent, float duration, float startDelay) {
        if (info.Clip == VClipID::None) return;
        if (info.Instances < 0) info.Instances = 1;
        if (duration <= 0) duration = startDelay + info.Delay.Max * info.Instances;

        ExplosionEffect e;
        e.StartDelay = startDelay;
        e.Duration = duration;
        e.Parent = parent;

        if (auto obj = Game::GetObject(parent)) {
            e.Position = obj->GetPosition(Game::LerpAmount);
            e.Segment = obj->Segment;
        }

        AddEffect(MakePtr<ExplosionEffect>(e));
    }

    void ExplosionEffect::OnUpdate(float /*dt*/, EffectID) {
        auto instances = Info.Instances;

        for (int i = 0; i < instances; i++) {
            if (Info.Sound != SoundID::None && i == 0) {
                Sound3D sound(Info.Sound);
                sound.Volume = Info.Volume;
                Sound::Play(sound, Position, Segment);
                //sound.Source = expl.Parent; // no parent so all nearby sounds merge
            }

            Info.Instances--;
            ParticleInfo info;
            auto position = Position;

            if (Info.UseParentVertices && Parent) {
                if (auto parent = Game::GetObject(Parent)) {
                    auto offset = GetRandomPointOnObject(*parent).Offset;
                    position = Vector3::Transform(offset, parent->GetTransform(Game::LerpAmount));
                    if (Info.Variance > 0) {
                        auto dir = position - parent->Position;
                        dir.Normalize();
                        position += dir * Random() * Info.Variance;
                    }
                }
            }
            else if (Info.Variance > 0) {
                position += Vector3(RandomN11() * Info.Variance, RandomN11() * Info.Variance, RandomN11() * Info.Variance);
            }

            info.Radius = Info.Radius.GetRandom();
            info.Clip = Info.Clip;
            info.Color = Info.Color;
            info.FadeTime = FadeTime;

            // only apply light to first explosion instance
            if (i == 0 && Info.LightColor != LIGHT_UNSET) {
                auto lightDuration = Resources::GetVideoClip(info.Clip).PlayTime * 0.75f;
                LightEffectInfo light;
                light.FadeTime = lightDuration;
                light.LightColor = Info.LightColor;
                light.Radius = Info.LightRadius > 0 ? Info.LightRadius : info.Radius * 4;
                AddLight(light, position, lightDuration, Segment);
            }

            AddParticle(info, Segment, position);

            if (Info.Instances > 1 && (Info.Delay.Min > 0 || Info.Delay.Max > 0)) {
                StartDelay = Info.Delay.GetRandom();
                break;
            }
        }
    }

    constexpr float TRACER_MIN_DIST_MULT = 0.75;

    void Tracer::OnUpdate(float /*dt*/, EffectID) {
        Direction = Position - PrevPosition;
        TravelDist += Direction.Length();
        Direction.Normalize();

        //if (TravelDist < Length * TRACER_MIN_DIST_MULT)
        //    Elapsed = 0; // Don't start effect until tracer clears the start
    }

    void Tracer::Draw(GraphicsContext& ctx) {
        auto& effect = Effects->SpriteAdditive;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        auto cmdList = ctx.GetCommandList();
        effect.Shader->SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
        effect.Shader->SetSampler(cmdList, Render::GetWrappedTextureSampler());

        if (TravelDist < Info.Length * TRACER_MIN_DIST_MULT) return; // don't draw tracers that are too short
        if (Direction == Vector3::Zero || PrevPosition == Position) return;

        const auto elapsed = GetElapsedTime();
        float fade = 1;
        if (GetRemainingTime() < FadeTime) {
            //fade = 1 - (FadeTime - remaining) / FadeTime;
        }
        else if (elapsed < FadeTime) {
            fade = 1 - (FadeTime - elapsed) / FadeTime;
            fade = elapsed / FadeTime;
        }

        fade = std::clamp(fade, 0.0f, 1.0f);

        const auto lenMult = 0.5f + fade * 0.5f;
        const auto head = Position;
        const auto tail = Position - Direction * Info.Length * lenMult;
        const auto normal = GetBeamNormal(head, tail, ctx.Camera);

        // draw rectangular segment
        const auto halfWidth = Info.Width * 0.5f;
        auto up = normal * halfWidth;
        auto color = Info.Color;
        color.w *= fade;

        if (!Info.Texture.empty()) {
            auto& material = Render::Materials->Get(Info.Texture);
            effect.Shader->SetDiffuse(cmdList, material.Handle());
            g_SpriteBatch->Begin(cmdList);

            ObjectVertex v0{ head + up, { 0, 1 }, color };
            ObjectVertex v1{ head - up, { 1, 1 }, color };
            ObjectVertex v2{ tail - up, { 1, 0 }, color };
            ObjectVertex v3{ tail + up, { 0, 0 }, color };
            g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
            g_SpriteBatch->End();
            Stats::DrawCalls++;
        }

        if (!Info.BlobTexture.empty() /*&& dist > Length*/) {
            auto& material = Render::Materials->Get(Info.BlobTexture);
            effect.Shader->SetDiffuse(cmdList, material.Handle());
            g_SpriteBatch->Begin(cmdList);

            auto right = ctx.Camera.GetRight() * halfWidth;
            up = ctx.Camera.Up * halfWidth;
            constexpr float BLOB_OFFSET = 0.25f; // tracer textures are thickest about a quarter from the end
            auto blob = head - Direction * Info.Length * BLOB_OFFSET * lenMult;

            ObjectVertex v0{ blob + up - right, { 0, 0 }, color };
            ObjectVertex v1{ blob - up - right, { 1, 0 }, color };
            ObjectVertex v2{ blob - up + right, { 1, 1 }, color };
            ObjectVertex v3{ blob + up + right, { 0, 1 }, color };
            g_SpriteBatch->DrawQuad(v0, v1, v2, v3);
            g_SpriteBatch->End();
            Stats::DrawCalls++;
        }
    }

    void AddTracer(const TracerInfo& info, ObjRef parent) {
        std::array tex = { info.Texture, info.BlobTexture };
        Render::Materials->LoadTextures(tex);

        Tracer tracer;
        tracer.Parent = parent;
        auto obj = Game::GetObject(parent);
        if (!obj) return;

        tracer.PrevPosition = tracer.Position = obj->Position;
        tracer.Segment = obj->Segment;

        tracer.Duration = 5;
        AddEffect(MakePtr<Tracer>(tracer));
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
            float remaining = decal.GetRemainingTime();
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

    void DrawDecals(GraphicsContext& ctx, float dt) {
        auto cmdList = ctx.GetCommandList();
        PIXScopedEvent(cmdList, PIX_COLOR_INDEX(0), "Decals");

        {
            auto& effect = Effects->SpriteMultiply;

            for (auto& decal : Decals) {
                if (IsExpired(decal)) continue;

                if (ctx.ApplyEffect(effect)) {
                    ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                    effect.Shader->SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
                    effect.Shader->SetSampler(cmdList, Render::GetWrappedTextureSampler());
                }

                decal.Update(dt, EffectID(0));

                auto& material = Render::Materials->Get(decal.Texture);
                effect.Shader->SetDiffuse(cmdList, material.Handle());
                g_SpriteBatch->Begin(cmdList);
                DrawDecal(decal, *g_SpriteBatch.get());
                g_SpriteBatch->End();
                Stats::DrawCalls++;
            }
        }

        {
            auto& effect = Effects->SpriteAdditiveBiased;

            for (auto& decal : AdditiveDecals) {
                if (IsExpired(decal)) continue;

                if (ctx.ApplyEffect(effect)) {
                    ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
                    effect.Shader->SetDepthTexture(cmdList, Adapter->LinearizedDepthBuffer.GetSRV());
                    effect.Shader->SetSampler(cmdList, Render::GetWrappedTextureSampler());
                }

                decal.Update(dt, EffectID(0));

                auto& material = Render::Materials->Get(decal.Texture);
                effect.Shader->SetDiffuse(cmdList, material.Handle());
                g_SpriteBatch->Begin(cmdList);
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
                decal.Duration = 0;
        }
    }

    void RemoveEffects(ObjRef id) {
        // Expire effects attached to an object when it is destroyed
        for (size_t effectId = 0; effectId < VisualEffects.size(); effectId++) {
            auto& effect = VisualEffects[effectId];
            if (effect && effect->Parent == id)
                effect->Duration = 0; // expire the effect
        }
    }

    void DetachEffects(EffectBase& effect) {
        // Had a parent but it was destroyed
        if (effect.FadeTime > 0) {
            // Detach from parent and fade out
            effect.Duration = float(Game::Time - effect.CreationTime) + effect.FadeTime;
            effect.Parent = {};
        }
        else {
            effect.Duration = 0; // Remove
        }
    }

    void DetachEffects(ObjRef id) {
        // Expire effects attached to an object when it is destroyed
        for (size_t effectId = 0; effectId < VisualEffects.size(); effectId++) {
            auto& effect = VisualEffects[effectId];
            if (effect && effect->Parent == id)
                DetachEffects(*effect);
        }
    }

    void SparkEmitter::OnInit() {
        _nextInterval = Info.Interval.GetRandom();
    }

    void SparkEmitter::OnUpdate(float dt, EffectID) {
        _nextInterval -= dt;

        if (_nextInterval <= 0) {
            auto count = Info.Count.GetRandom();
            for (uint i = 0; i < count; i++)
                CreateSpark();

            if (Info.Interval.Min == Info.Interval.Max && Info.Interval.Min == 0)
                _nextInterval = FLT_MAX;
            else
                _nextInterval = Info.Interval.GetRandom();
        }

        auto parent = Game::GetObject(Parent);

        Vector3 parentPos = parent ? parent->GetPosition(Game::LerpAmount) : Vector3::Zero;
        Vector3 parentDelta = parent ? parentPos - PrevParentPosition : Vector3::Zero;
        if (parent) PrevParentPosition = parentPos;

        for (auto& spark : _sparks) {
            if (!spark.IsAlive()) continue;

            spark.PrevPosition = spark.Position;
            spark.PrevVelocity = spark.Velocity;

            if (Info.UseWorldGravity)
                spark.Velocity += Game::Gravity * dt;

            if (Info.UsePointGravity) {
                auto center = Position;
                if (parent && (Info.PointGravityVelocity != Vector3::Zero || Info.PointGravityOffset != Vector3::Zero)) {
                    // Offset the gravity center over the lifetime of the particle
                    auto t = Info.SparkDuration.Max - (Info.SparkDuration.Max - spark.Life);
                    center += Vector3::Transform(Info.PointGravityVelocity * t + Info.PointGravityOffset + ParentSubmodel.Offset, parent->Rotation);
                }

                auto dir = center - spark.Position;
                dir.Normalize();
                spark.Velocity += dir * Info.PointGravityStrength * dt;
            }

            if (parent && Info.Relative)
                spark.Position += parentDelta; // Move particle with parent

            spark.Position += spark.Velocity * dt;
        }
    }

    void SparkEmitter::OnFixedUpdate(float dt, EffectID) {
        for (auto& spark : _sparks) {
            spark.Life -= dt;
            if (!spark.IsAlive()) continue;

            if (dt > 0)
                spark.Velocity *= 1 - Info.Drag;

            if (Info.Physics) {
                auto dir = spark.Velocity;
                dir.Normalize();

                Ray ray(spark.Position, dir);
                auto rayLen = Vector3::Distance(spark.PrevPosition, spark.Position) * 1.2f;
                LevelHit hit;
                RayQuery query{ .MaxDistance = rayLen, .Start = spark.Segment };
                bool hitSomething = Game::Intersect.RayLevel(ray, query, hit);

                if (!hitSomething) {
                    // check surrounding segments
                    auto& seg = Game::Level.GetSegment(spark.Segment);
                    for (auto& side : SIDE_IDS) {
                        query.Start = seg.GetConnection(side);
                        hitSomething = Game::Intersect.RayLevel(ray, query, hit);
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
                        spark.Velocity -= hit.Normal * hit.Normal.Dot(spark.Velocity) * (1 - Info.Restitution);
                        spark.Velocity = Vector3::Reflect(spark.Velocity, hit.Normal);
                        spark.Segment = hit.Tag.Segment;
                    }
                }
            }
        }
    }

    void SparkEmitter::Draw(GraphicsContext& ctx) {
        auto& effect = Effects->SpriteAdditive;
        ctx.ApplyEffect(effect);
        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());
        auto cmdList = ctx.GetCommandList();

        effect.Shader->SetSampler(cmdList, Render::GetClampedTextureSampler());
        auto& material = Render::Materials->Get(Info.Texture);
        effect.Shader->SetDiffuse(cmdList, material.Handle());
        g_SpriteBatch->Begin(cmdList);

        auto remaining = GetRemainingTime();
        float fade = remaining < FadeTime ? remaining / FadeTime : 1; // global emitter fade

        for (auto& spark : _sparks) {
            if (spark.Life <= 0) continue;
            auto pos = spark.Position;
            auto vec = spark.Position - spark.PrevPosition;
            vec.Normalize();

            Vector3 head = pos + vec * Info.Width * 0.5;
            Vector3 tail = pos - vec * Info.Width * 0.5;

            auto size = spark.Velocity * Info.VelocitySmear;
            head += size;
            tail -= size;

            auto tangent = GetBeamNormal(head, tail, ctx.Camera) * Info.Width * 0.5f;

            auto color = Info.Color;
            if (FadeTime > 0) {
                auto t = 1 - std::clamp((FadeTime - spark.Life) / FadeTime, 0.0f, 1.0f);
                color.w = t * fade;
                tangent *= t;
            }

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
        spark.Life = Info.SparkDuration.GetRandom();
        auto position = Position;
        if (Info.SpawnRadius > 0)
            position += RandomPointOnSphere() * Info.SpawnRadius;

        spark.Position = spark.PrevPosition = position;
        spark.Segment = Segment;

        if (Info.Direction == Vector3::Zero) {
            spark.Velocity = RandomVector(Info.Velocity.GetRandom());
        }
        else {
            auto spread = RandomPointOnHemisphere();
            auto right = Info.Direction.Cross(Info.Up);
            //auto direction = Direction;
            Vector3 direction;
            direction += right * spread.x * Info.ConeRadius;
            direction += Info.Up * spread.y * Info.ConeRadius;
            direction += Info.Direction * spread.z;
            spark.Velocity = direction * Info.Velocity.GetRandom();
        }

        if (auto parent = Game::GetObject(Parent)) {
            PrevParentPosition = parent->Position;

            //if (Offset != Vector3::Zero)
            spark.Position += Vector3::Transform(ParentSubmodel.Offset + Info.Offset, parent->Rotation);
        }

        _sparks.Add(spark);
    }

    void AddSparkEmitter(const SparkEmitterInfo& info, SegID seg, const Vector3& worldPos) {
        if (info.Color == LIGHT_UNSET) return;
        SparkEmitter emitter;
        emitter.Info = info;
        emitter.Segment = seg;
        emitter.Position = worldPos;
        emitter.Info.Color.Premultiply();
        emitter.Info.Color.w = 0;

        //if (auto parent = Game::GetObject(emitter.Parent)) {
        //    emitter.Position = parent->GetPosition(Game::LerpAmount);
        //}

        Render::Materials->LoadTexture(info.Texture);
        if (emitter.Duration == 0) emitter.Duration = info.SparkDuration.Max;
        AddEffect(MakePtr<SparkEmitter>(std::move(emitter)));
    }

    void AddSparkEmitter(const SparkEmitterInfo& info, ObjRef parent, const Vector3& offset) {
        if (info.Color == LIGHT_UNSET) return;
        SparkEmitter emitter;
        emitter.Info = info;
        emitter.Info.Color.Premultiply();
        emitter.Info.Color.w = 0;
        emitter.Parent = parent;
        emitter.ParentSubmodel.Offset = offset;

        if (auto obj = Game::GetObject(parent)) {
            emitter.Position = obj->GetPosition(Game::LerpAmount);
            emitter.Segment = obj->Segment;
        }

        Render::Materials->LoadTexture(info.Texture);
        if (emitter.Duration == 0) emitter.Duration = info.SparkDuration.Max;
        AddEffect(MakePtr<SparkEmitter>(std::move(emitter)));
    }

    EffectID AddLight(LightEffectInfo& info, const Vector3& position, float duration, SegID segment) {
        ASSERT(duration > 0);
        if (info.Radius <= 0 || info.LightColor == LIGHT_UNSET) return EffectID::None;
        LightEffect light(info);
        light.Info.LightColor.Premultiply();
        light.Info.LightColor.w = 1;
        light.Duration = duration;
        light.FadeTime = info.FadeTime;
        light.Segment = segment;
        light.Position = position;
        return AddEffect(make_unique<LightEffect>(std::move(light)));
    }

    EffectID AttachLight(const LightEffectInfo& info, ObjRef parent, SubmodelRef submodel) {
        auto obj = Game::GetObject(parent);
        if (!obj || info.Radius <= 0 || info.LightColor == LIGHT_UNSET) return EffectID::None;

        LightEffect light(info);
        light.Info.LightColor.Premultiply();
        light.Info.LightColor.w = 1;
        light.Duration = MAX_OBJECT_LIFE; // lights will be removed when their parent is destroyed
        light.FadeTime = info.FadeTime;
        light.Parent = parent;
        light.ParentSubmodel = submodel;
        light.Position = obj->GetPosition(Game::LerpAmount);
        light.Segment = obj->Segment;

        return AddEffect(make_unique<LightEffect>(std::move(light)));
    }

    void UpdateEffect(float dt, EffectID id) {
        if (auto& effect = VisualEffects[(int)id]) {
            effect->Update(dt, id);
        }
    }

    void UpdateAllEffects(float dt) {
        LegitProfiler::ProfilerTask task("Update effects");

        for (size_t effectId = 0; effectId < VisualEffects.size(); effectId++) {
            if (auto& effect = VisualEffects[effectId])
                effect->Update(dt, EffectID(effectId));
        }

        // Expire effects in case other effects add new ones mid-frame
        for (size_t effectId = 0; effectId < VisualEffects.size(); effectId++) {
            auto& effect = VisualEffects[effectId];
            if (effect && IsExpired(*effect)) {
                effect->OnExpire();

                UnlinkEffect(*effect, EffectID(effectId));
                VisualEffects[effectId].reset();
            }
        }

        LegitProfiler::AddCpuTask(std::move(task));
    }

    void FixedUpdateEffects(float dt) {
        if (VisualEffects.size() + 100 > VisualEffects.capacity()) {
            VisualEffects.resize(VisualEffects.size() + 100);
            SPDLOG_WARN("Resizing visual effects buffer to {}", VisualEffects.size());
        }

        for (size_t effectId = 0; effectId < VisualEffects.size(); effectId++) {
            auto& effect = VisualEffects[effectId];
            if (effect)
                effect->FixedUpdate(dt, EffectID(effectId));
        }
    }

    // Updates owners and releases expired effects
    void EndUpdateEffects() {
        for (int id = 0; id < VisualEffects.size(); id++) {
            auto& effect = VisualEffects[id];
            if (!effect) continue;

            // Relink effects in case their parent segment changed
            auto parent = Game::GetObject(effect->Parent);
            if (parent && parent->IsAlive()) {
                if (parent->Segment != effect->Segment) {
                    LinkEffect(*effect, (EffectID)id, parent->Segment);
                }
            }

            // Remove dead effects
            if (IsExpired(*effect)) {
                effect->OnExpire();

                UnlinkEffect(*effect, (EffectID)id);
                effect.reset();
            }
        }
    }

    void ResetEffects() {
        for (auto& seg : Game::Level.Segments)
            seg.Effects.clear();

        VisualEffects.clear();
        VisualEffects.reserve(200);

        //Beams.Clear();

        for (auto& decal : Decals)
            decal.Duration = 0;
    }

    void EffectBase::Update(float dt, EffectID id) {
        StartDelay -= dt;
        if (StartDelay > 0 /*|| Updates > 0*/) return;
        PrevPosition = Position;

        if (Parent && !UpdatePositionFromParent())
            DetachEffects(*this);

        OnUpdate(dt, id);
    }

    void EffectBase::FixedUpdate(float dt, EffectID id) {
        OnFixedUpdate(dt, id);
    }

    bool EffectBase::UpdatePositionFromParent() {
        auto parent = Game::GetObject(Parent);
        //if (!parent || !parent->IsAlive() || HasFlag(parent->Flags, ObjectFlag::Destroyed))
        if (!parent || !parent->IsAlive())
            return false;

        auto pos = parent->GetPosition(Game::LerpAmount);
        if (ParentSubmodel) {
            auto offset = GetSubmodelOffset(*parent, ParentSubmodel);
            pos += Vector3::Transform(offset, parent->GetRotation(Game::LerpAmount));
        }

        Position = pos;
        return true;
    }

    void ScanNearbySegments(const Level& level, SegID start, const Vector3& point, float radius, const std::function<void(const Segment&)>& action) {
        struct Visited {
            SegID id = SegID::None, parent = SegID::None;
        };

        static List<SegID> queue;
        queue.clear();
        queue.reserve(16);
        queue.push_back(start);

        int index = 0;

        while (index < queue.size()) {
            auto segid = queue[index++];
            auto seg = level.TryGetSegment(segid);
            if (!seg) continue;

            action(*seg);

            for (auto& sideid : SIDE_IDS) {
                auto& side = seg->GetSide(sideid);
                Plane plane(side.Center, side.AverageNormal);
                if (plane.DotCoordinate(point) > radius)
                    continue; // point too far from side

                auto connection = seg->GetConnection(sideid);
                if (!Seq::contains(queue, connection)) {
                    queue.push_back(connection);
                }
            }
        }
    }

    void LightEffect::OnUpdate(float /*dt*/, EffectID id) {
        float lightRadius = Info.Radius;
        Color lightColor = Info.LightColor;

        if (FadeTime > 0) {
            auto t = std::clamp(GetRemainingTime() / FadeTime, 0.0f, 1.0f);
            if (t <= 0) return; // Invisible at t = 0
            //lightRadius = std::lerp(lightRadius * 0.75f, lightRadius, t);
            lightColor = Color::Lerp(Color(0, 0, 0), lightColor, t);
        }

        if (Info.Mode == DynamicLightMode::Flicker || Info.Mode == DynamicLightMode::StrongFlicker) {
            //constexpr float FLICKER_INTERVAL = 15; // hz
            //float interval = std::floor(Render::ElapsedTime * FLICKER_INTERVAL + (float)obj.Signature * 0.1747f) / FLICKER_INTERVAL;
            const float flickerSpeed = Info.Mode == DynamicLightMode::Flicker ? 4.0f : 6.0f;
            const float flickerRadius = Info.Mode == DynamicLightMode::Flicker ? 0.03f : 0.04f;
            // slightly randomize the radius and brightness on an interval
            auto noise = OpenSimplex2::Noise2((int)id, Game::Time * flickerSpeed, 0);
            lightRadius += lightRadius * noise * flickerRadius;

            if (Info.Mode == DynamicLightMode::StrongFlicker)
                lightColor *= 1 + noise * 0.025f;
        }
        else if (Info.Mode == DynamicLightMode::Pulse) {
            lightRadius += lightRadius * sinf((float)Game::Time * 3.14f * 1.25f + (float)id * 0.1747f) * 0.125f;
        }
        else if (Info.Mode == DynamicLightMode::BigPulse) {
            lightRadius += lightRadius * sinf((float)Game::Time * 3.14f * 1.25f + (float)id * 0.1747f) * 0.25f;
        }

        Graphics::LightData light{};
        light.radius = lightRadius;
        light.color = lightColor;
        light.type = LightType::Point;
        light.pos = Position;
        Graphics::Lights.AddLight(light);

        if (Game::GetState() == GameState::Editor || Info.SpriteMult <= 0)
            return;

        ScanNearbySegments(Game::Level, Segment, Position, lightRadius, [&light, mult = Info.SpriteMult](const Inferno::Segment& seg) {
            for (auto& objid : seg.Objects) {
                if (auto obj = Game::Level.TryGetObject(objid)) {
                    auto& render = obj->Render;
                    if (render.Type == RenderType::Hostage || render.Type == RenderType::Powerup || objid == ObjID(0)) {
                        if (render.Emissive != Color()) continue;

                        auto dist = Vector3::Distance(light.pos, obj->GetPosition(Game::LerpAmount));
                        if (dist > light.radius) continue;
                        auto falloff = 1 - std::clamp(dist / light.radius, 0.0f, 1.0f);

                        if (objid == ObjID(0))
                            Game::Player.DirectLight += light.color * falloff * mult;
                        else
                            render.VClip.DirectLight += light.color * falloff * mult;
                    }
                }
            }
        });
    }
}
