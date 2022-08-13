#pragma once

#include "EffectClip.h"
#include "DirectX.h"
#include "Graphics/CommandContext.h"

namespace Inferno::Render {
    struct Particle {
        VClipID Clip = VClipID::None;
        Vector3 Position;
        Vector3 Up = Vector3::Zero;
        Color Color = { 1, 1, 1 };
        float Radius = 1;
        float Rotation = 0;
        float Life = 0;
        float FadeTime = 0; // How long it takes to fade the particle out
        float Delay = 0;
        //float FadeDuration = 0;
        ObjID Parent = ObjID::None;
        Vector3 ParentOffset;

        static bool IsAlive(const Particle& p) { return p.Life > 0; }
    };

    struct ParticleEmitterInfo {
        VClipID Clip = VClipID::None;
        float Life = 0; // How long the emitter lives for
        ObjID Parent = ObjID::None; // Moves with this object
        Vector3 ParentOffset; // Offset from parent
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

        Particle CreateParticle() {
            auto& vclip = Resources::GetVideoClip(Clip);

            Particle p;
            p.Color = Color;
            p.Clip = Clip;
            p.Life = vclip.PlayTime;
            p.Parent = Parent;
            p.ParentOffset = ParentOffset;
            p.Position = Position;
            p.Radius = MinRadius + Random() * (MaxRadius - MinRadius);

            if (RandomRotation)
                p.Rotation = Random() * DirectX::XM_2PI;

            return p;
        }
    };

    class ParticleEmitter {
        float _spawnTimer = 0; // internal timer for when to create a particle
        float _life = 0;
        float _startDelay = 0;
        DataPool<Particle> _particles;
        ParticleEmitterInfo _info;
    public:
        ParticleEmitter(ParticleEmitterInfo& info, size_t capacity)
            : _info(info), _particles(Particle::IsAlive, capacity) {
            _startDelay = info.StartDelay;
            Position = info.Position;
        }

        Vector3 Position;

        span<const Particle> GetParticles() const { return _particles.GetLiveData(); }
        void AddParticle() {
            _particles.Add(_info.CreateParticle());
        }

        void Update(float dt);

        static bool IsAlive(const ParticleEmitter& p) { return p._life > 0; }
    };

    void AddEmitter(ParticleEmitter& emitter, size_t capacity);
    void AddParticle(Particle&, bool randomRotation = true);

    void UpdateParticles(Level&, float dt);
    void QueueParticles();
    void DrawParticles(Graphics::GraphicsContext& ctx);
}