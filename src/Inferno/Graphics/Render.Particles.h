#pragma once

#include "DataPool.h"
#include "EffectClip.h"
#include "DirectX.h"
#include "Game.Object.h"
#include "Render.Beam.h"
#include "Render.Effect.h"
#include "Graphics/CommandContext.h"

namespace Inferno {
    struct Level;
}

namespace Inferno::Render {
    struct RenderCommand;

    bool IsExpired(const EffectBase& effect);

    void AddDebris(const DebrisInfo& info, const Matrix& transform, SegID seg, const Vector3& velocity, const Vector3& angularVelocity, float duration);

    void CreateExplosion(ExplosionEffectInfo&, SegID, const Vector3& position, float duration = 0, float startDelay = 0);
    void CreateExplosion(ExplosionEffectInfo&, ObjRef parent, float duration = 0, float startDelay = 0);

    struct Tracer final : EffectBase {
        TracerInfo Info;

        // Runtime vars
        //Vector3 End; // Updated in realtime. Used to fade out tracer after object dies.
        //float Fade = 0; // For fading the tracer in and out

        float TravelDist = 0;
        Vector3 Direction; // Motion vector of the tracer

        void OnUpdate(float dt, EffectID) override;
        void Draw(GraphicsContext&) override;
    };

    // Adds a tracer effect attached to an object that is removed when the parent object dies.
    // Tracers are only drawn when the minimum length is reached
    void AddTracer(const TracerInfo&, ObjRef parent);

    struct DecalInfo final : EffectBase {
        Vector3 Normal, Tangent, Bitangent;
        string Texture = "scorchB";

        float Radius = 2;
        float FadeRadius = 3.0; // Radius to grow to at end of life

        Color Color = { 1, 1, 1 };
        SideID Side;
        bool Additive = false;
    };

    struct Spark {
        float Life = 0;
        Vector3 Velocity, PrevVelocity;
        Vector3 Position, PrevPosition;
        SegID Segment = SegID::None;
        bool IsAlive() const { return Life > 0; }
    };

    class SparkEmitter final : public EffectBase {
        DataPool<Spark> _sparks = { &Spark::IsAlive, 100 };
        float _nextInterval = 0;

    public:
        SparkEmitterInfo Info;
        Vector3 PrevParentPosition;

        SparkEmitter() { Queue = RenderQueueType::Transparent; }

        void OnInit() override;
        void OnUpdate(float dt, EffectID) override;
        void OnFixedUpdate(float dt, EffectID) override;
        void Draw(GraphicsContext&) override;

    private:
        void CreateSpark();
    };

    //void AddSparkEmitter(SparkEmitter&);
    void AddDecal(DecalInfo& decal);
    void DrawDecals(GraphicsContext& ctx, float dt);
    span<DecalInfo> GetAdditiveDecals();
    span<DecalInfo> GetDecals();

    // Removes decals on a side
    void RemoveDecals(Tag);

    // Removes all effects associated with an object
    void RemoveEffects(ObjRef);

    // Detach effects from an object and cause them to fade out
    void DetachEffects(ObjRef);

    void AddBeam(const BeamInfo&, SegID seg, float duration, const Vector3& start, const Vector3& end);
    void AddBeam(const BeamInfo&, float duration, ObjRef start, const Vector3& end, int startGun);
    void AttachBeam(const BeamInfo&, float duration, ObjRef start, ObjRef end = {}, int startGun = -1);
    
    void AddParticle(const ParticleInfo&, SegID, const Vector3& position);
    void AttachParticle(const ParticleInfo&, ObjRef parent, SubmodelRef submodel = {});

    void AddSparkEmitter(const SparkEmitterInfo&, SegID, const Vector3& worldPos = Vector3::Zero);
    void AddSparkEmitter(const SparkEmitterInfo& info, ObjRef parent, const Vector3& offset = Vector3::Zero);

    void ScanNearbySegments(const Level& level, SegID start, const Vector3& point, float radius, const std::function<void(const Segment&)>& action);

    EffectID AddLight(LightEffectInfo& info, const Vector3& position, float duration, SegID segment);
    EffectID AttachLight(const LightEffectInfo& info, ObjRef parent, SubmodelRef submodel);

    // Gets a visual effect
    EffectBase* GetEffect(EffectID effect);

    void ResetEffects();
    void UpdateEffect(float dt, EffectID id);

    // Either call this or individual effects using UpdateEffect()
    void UpdateAllEffects(float dt);
    void FixedUpdateEffects(float dt);
    void EndUpdateEffects();

    namespace Stats {
        inline uint EffectDraws = 0;
    }
}
