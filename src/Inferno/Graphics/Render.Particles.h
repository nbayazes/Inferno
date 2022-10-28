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

    // Remains of a destroyed robot
    struct Debris {
        float Life = 0;
        Matrix Transform, PrevTransform;
        //Vector3 Position, LastPosition;
        //Matrix3x3 Rotation, LastRotation;
        Vector3 Velocity;
        Vector3 AngularVelocity;
        float Mass = 1;
        float Drag = 0.01f;
        float Radius = 1;
        ModelID Model = ModelID::None;
        int Submodel = 0;
        SegID Segment;
        TexID TexOverride = TexID::None;

        static bool IsAlive(const Debris& d) { return d.Life > 0; }
    };

    void AddDebris(Debris& debris);
    void UpdateDebris(float dt);
    void QueueDebris();

    struct ExplosionInfo {
        ObjID Parent = ObjID::None;
        SegID Segment = SegID::None;
        VClipID Clip = VClipID::SmallExplosion; // Default explosion
        SoundID Sound = SoundID::None;
        float MinRadius = 2.5f, MaxRadius = 2.5f;
        float Variance = 0; // Position variance
        int Instances = 1; // how many explosions to create
        float MinDelay = 0.25f, MaxDelay = 0.75f; // how long to wait before creating the next explosion instance
        float InitialDelay = -1; // how long to wait before creating any explosions
        Color Color = { 2, 2, 2 }; // Particle color
        Vector3 Position;
        float FadeTime = 0; // How long it takes to fade the particles out

        static bool IsAlive(const ExplosionInfo& info) { return info.InitialDelay >= 0; }
    };

    void CreateExplosion(ExplosionInfo&);
    void UpdateExplosions(float dt);

    //enum class BeamFlag {
    //    SineNoise, RandomEnd
    //};

    struct BeamInfo {
        Vector3 Start;
        Vector3 End;
        ObjID StartObj = ObjID::None; // NYI: attaches beam to this object
        ObjID EndObj = ObjID::None; // NYI: attaches beam to this object
        float Radius = 0; // If End is none, randomly strike targets within this radius
        float Width = 2.0f;
        float Life = 0;
        Color Color = { 1, 1, 1 };
        float Noise = 0;
        string Texture;
        float ScrollSpeed = 0; // Texture scroll speed
        float Frequency = 1 / 60.0f; // How often in seconds to recalculate noise
        SegID Segment;
        float Scale = 1; // Scale for texture vs beam width
        bool SineNoise = false; // Sine noise when true, Fractal noise when false
        bool RandomEnd = false; // Uses a random end point
        float Time = 0; // animates noise and determines the phase
        float Amplitude = 0; // Peak to peak height of noise. 0 for straight beam.

        struct {
            float Length;
            int Segments;
            List<float> Noise;
            float NextUpdate;
        } Runtime{};

        static bool IsAlive(const BeamInfo& info) { return info.Life > 0; }
    };

    void AddBeam(BeamInfo&);
    void DrawBeams(Graphics::GraphicsContext& ctx);

    struct TracerInfo {
        ObjID Parent = ObjID::None; // Object the tracer is attached to. Normally a weapon projectile.
        ObjSig Signature = {};
        float Length = 20; // How long the tracer is
        float Width = 2;
        string Texture, BlobTexture;
        Color Color = { 1, 1, 1 };
        float FadeSpeed = 0.125f; // How quickly the tracer fades in and out

        // Runtime vars
        Vector3 Start;
        Vector3 End; // Updated in realtime. Used to fade out tracer after object dies.
        float Fade = 0; // For fading the tracer in and out
        bool ParentIsLive = false;
        float Life = 0;
        static bool IsAlive(const TracerInfo& info) { return info.Life > 0; }
    };

    // Adds a tracer effect attached to an object that is removed when the object dies.
    // Tracers are only drawn when the minimum length is reached
    void AddTracer(TracerInfo&);
    void DrawTracers(Graphics::GraphicsContext& ctx);

    struct DecalInfo {
        Vector3 Position;
        Vector3 Tangent, Bitangent;
        string Texture = "scorchB";

        float Size = 2;
        Color Color = { 1, 1, 1 };
        float Life = 0;
        WallID Wall = WallID::None; // For decals placed on walls
    };

    void AddDecal(DecalInfo& decal);
    void DrawDecals(Graphics::GraphicsContext& ctx);

    // Removes decals attached to a wall
    void RemoveDecals(WallID front, WallID back);
}