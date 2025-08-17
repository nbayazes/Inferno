#pragma once

#include "DataPool.h"
#include "EffectClip.h"
#include "DirectX.h"
#include "Render.Beam.h"
#include "Render.Effect.h"

namespace Inferno {
    struct Level;
}

namespace Inferno::Render {
    struct RenderCommand;

    bool IsExpired(const EffectBase& effect);

    struct Particle final : EffectBase {
        explicit Particle(const ParticleInfo& info) : Info(info) {}
        ParticleInfo Info;
        //float FadeDuration = 0;

        void Draw(GraphicsContext&) override;
    };

    // An explosion can consist of multiple particles
    struct ExplosionEffect : EffectBase {
        explicit ExplosionEffect(const ExplosionEffectInfo& info) : Info(info) {
            Queue = RenderQueueType::None;
        }

        ExplosionEffectInfo Info;

        //bool IsAlive() const { return InitialDelay >= 0; }
        void OnUpdate(float, EffectID) override;
    };

    struct Tracer final : EffectBase {
        explicit Tracer(TracerInfo info): Info(std::move(info)) {}
        TracerInfo Info;

        // Runtime vars
        //Vector3 End; // Updated in realtime. Used to fade out tracer after object dies.
        //float Fade = 0; // For fading the tracer in and out

        float TravelDist = 0;
        Vector3 Direction; // Motion vector of the tracer

        void OnUpdate(float dt, EffectID) override;
        void Draw(GraphicsContext&) override;
    };

    struct Spark {
        float Life = 0;
        Vector3 Velocity, PrevVelocity;
        Vector3 Position, PrevPosition;
        SegID Segment = SegID::None;
        float Width = 1;
        bool IsAlive() const { return Life > 0; }
    };

    class SparkEmitter final : public EffectBase {
        DataPool<Spark> _sparks = { &Spark::IsAlive, 100 };
        float _nextInterval = 0;

    public:
        explicit SparkEmitter(const SparkEmitterInfo& info) : Info(info) {
            Queue = RenderQueueType::Transparent;
        }

        SparkEmitterInfo Info;

        void OnInit() override;
        void OnUpdate(float dt, EffectID) override;
        void OnFixedUpdate(float dt, EffectID) override;
        void Draw(GraphicsContext&) override;

    private:
        void CreateSpark();
    };

    struct LightEffect final : EffectBase {
        explicit LightEffect(const LightEffectInfo& info) : Info(info) { Queue = RenderQueueType::None; }
        LightEffectInfo Info;

        void OnUpdate(float dt, EffectID) override;

    private:
        Color _currentColor;
        float _currentRadius = 0;
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
        void DrawFog(GraphicsContext&) override;
        void DepthPrepass(GraphicsContext&) override;
        void OnFixedUpdate(float dt, EffectID) override;
        void OnExpire() override;
    };

    namespace Stats {
        inline uint EffectDraws = 0;
    }
}
