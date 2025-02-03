#include "pch.h"
#include "VisualEffects.h"
#include "Render.Effect.h"
#include "Game.h"
#include "Game.Segment.h"
#include "Graphics.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Particles.h"
#include "Render.Beam.h"
#include "Resources.h"

namespace Inferno {
    using namespace Render;

    void AddBeamInstance(BeamInstance& beam) {
        auto segId = FindContainingSegment(Game::Level, beam.Start);
        if (segId != SegID::None) beam.Segment = segId;

        std::array tex = { beam.Info.Texture };
        Render::Materials->LoadTextures(tex);

        if (beam.Info.HasRandomEndpoints())
            beam.InitRandomPoints(Game::GetObject(beam.Parent));

        beam.Length = (beam.Start - beam.End).Length();
        beam.Width = beam.Info.Width.GetRandom();
        beam.OffsetU = Random();

        AddEffect(make_unique<BeamInstance>(beam));
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
        p.FadeTime = info.FadeTime;

        if (info.RandomRotation)
            p.Info.Rotation = Random() * DirectX::XM_2PI;

        Graphics::LoadTexture(info.Clip);
        AddEffect(make_unique<Particle>(p));
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
        p.FadeTime = info.FadeTime;

        if (info.RandomRotation)
            p.Info.Rotation = Random() * DirectX::XM_2PI;

        Graphics::LoadTexture(info.Clip);
        AddEffect(make_unique<Particle>(p));
    }


    void AddDebris(const DebrisInfo& info, const Matrix& transform, SegID seg, const Vector3& velocity, const Vector3& angularVelocity, float duration) {
        Debris debris(info);
        debris.Segment = seg;
        debris.Velocity = velocity;
        debris.AngularVelocity = angularVelocity;
        debris.Duration = duration;
        debris.Transform = debris.PrevTransform = transform;

        AddEffect(make_unique<Debris>(debris));
    }

    void CreateExplosion(ExplosionEffectInfo& info, SegID seg, const Vector3& position, float duration, float startDelay) {
        if (info.Clip == VClipID::None) return;
        if (info.Instances < 0) info.Instances = 1;
        if (duration <= 0) duration = startDelay + info.Delay.Max * info.Instances;

        ExplosionEffect e(info);
        e.Segment = seg;
        e.Position = position;
        e.StartDelay = startDelay;
        e.Duration = duration;
        e.FadeTime = info.FadeTime;

        AddEffect(make_unique<ExplosionEffect>(e));
    }

    void CreateExplosion(ExplosionEffectInfo& info, ObjRef parent, float duration, float startDelay) {
        if (info.Clip == VClipID::None) return;
        if (info.Instances < 0) info.Instances = 1;
        if (duration <= 0) duration = startDelay + info.Delay.Max * info.Instances;

        ExplosionEffect e(info);
        e.StartDelay = startDelay;
        e.Duration = duration;
        e.Parent = parent;
        e.FadeTime = info.FadeTime;

        if (auto obj = Game::GetObject(parent)) {
            e.Position = obj->GetPosition(Game::LerpAmount);
            e.Segment = obj->Segment;
        }

        AddEffect(make_unique<ExplosionEffect>(e));
    }


    void AddSparkEmitter(const SparkEmitterInfo& info, SegID seg, const Vector3& worldPos) {
        if (info.Color == LIGHT_UNSET) return;
        SparkEmitter emitter(info);
        emitter.Segment = seg;
        emitter.Position = worldPos;
        emitter.Duration = info.Duration.Max;
        emitter.FadeTime = info.FadeTime;
        PremultiplyColor(emitter.Info.Color);

        //if (auto parent = Game::GetObject(emitter.Parent)) {
        //    emitter.Position = parent->GetPosition(Game::LerpAmount);
        //}

        Render::Materials->LoadTexture(info.Texture);
        AddEffect(make_unique<SparkEmitter>(emitter));
    }

    void AttachSparkEmitter(const SparkEmitterInfo& info, ObjRef parent, const Vector3& offset) {
        if (info.Color == LIGHT_UNSET) return;
        SparkEmitter emitter(info);
        PremultiplyColor(emitter.Info.Color);
        emitter.Parent = parent;
        emitter.ParentSubmodel.offset = offset;
        emitter.Duration = MAX_OBJECT_LIFE; // Expire when parent dies
        emitter.FadeTime = info.FadeTime;

        if (auto obj = Game::GetObject(parent)) {
            emitter.Position = obj->GetPosition(Game::LerpAmount);
            emitter.Segment = obj->Segment;
        }

        Render::Materials->LoadTexture(info.Texture);
        AddEffect(make_unique<SparkEmitter>(emitter));
    }

    EffectID AddLight(const LightEffectInfo& info, const Vector3& position, float duration, SegID segment) {
        ASSERT(duration > 0);
        if (info.Radius <= 0 || info.LightColor == LIGHT_UNSET) return EffectID::None;
        LightEffect light(info);
        PremultiplyColor(light.Info.LightColor);
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
        PremultiplyColor(light.Info.LightColor);
        light.Duration = MAX_OBJECT_LIFE; // lights will be removed when their parent is destroyed
        light.FadeTime = info.FadeTime;
        light.Parent = parent;
        light.ParentSubmodel = submodel;
        light.Position = obj->GetPosition(Game::LerpAmount);
        light.Segment = obj->Segment;

        return AddEffect(make_unique<LightEffect>(light));
    }


    void AddTracer(const TracerInfo& info, ObjRef parent) {
        std::array tex = { info.Texture, info.BlobTexture };
        Render::Materials->LoadTextures(tex);

        Tracer tracer(info);
        tracer.Parent = parent;
        auto obj = Game::GetObject(parent);
        if (!obj) return;

        tracer.PrevPosition = tracer.Position = obj->Position;
        tracer.Segment = obj->Segment;
        tracer.FadeTime = info.FadeTime;
        tracer.Duration = 5;
        AddEffect(make_unique<Tracer>(tracer));
    }

    void AddDecal(const Decal& info, Tag tag, const Vector3& position, const Vector3& normal, const Vector3& tangent, float duration) {
        if (!Render::Materials->LoadTexture(info.Texture))
            return;

        DecalInstance decal;
        decal.Info = info;
        decal.Duration = duration;
        decal.Side = tag.Side;
        decal.Segment = tag.Segment;
        decal.Position = position;
        decal.Normal = normal;
        decal.Tangent = tangent;
        decal.Bitangent = tangent.Cross(normal);
        decal.FadeTime = info.FadeTime;

        //if (info.Additive) {
        //    AdditiveDecals[AdditiveDecalIndex++] = decal;

        //    if (AdditiveDecalIndex >= AdditiveDecals.size())
        //        AdditiveDecalIndex = 0;
        //}
        //else {
        //    Decals[DecalIndex++] = decal;

        //    if (DecalIndex >= Decals.size())
        //        DecalIndex = 0;
        //}
    }

    void RemoveDecals(Tag tag) {
        if (!tag) return;
        auto cside = Game::Level.GetConnectedSide(tag);

        for (auto& decal : GetDecals()) {
            Tag decalTag = { decal.Segment, decal.Side };
            if (decalTag == tag || (cside && decalTag == cside))
                decal.Duration = 0;
        }

        for (auto& decal : GetAdditiveDecals()) {
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

    void DetachEffects(ObjRef id) {
        // Expire effects attached to an object when it is destroyed
        for (size_t effectId = 0; effectId < VisualEffects.size(); effectId++) {
            auto& effect = VisualEffects[effectId];
            if (effect && effect->Parent == id)
                DetachEffects(*effect);
        }
    }

    void StopEffect(EffectID id) {
        if (auto effect = GetEffect(id))
            effect->Duration = 0;
    }

    void ResetEffects() {
        Render::ResetEffects();
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
}
