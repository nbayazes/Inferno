#pragma once

#include "EffectClip.h"
#include "DirectX.h"
#include "Graphics/CommandContext.h"

namespace Inferno::Render {
    struct RenderCommand;
    float GetRenderDepth(const Vector3& pos);

    //enum class EffectType {
    //    None, Particle, Emitter, Debris, Tracer
    //};

    struct EffectBase {
        SegID Segment = SegID::None;
        Vector3 Position;
        float Life = 0;
        bool IsTransparent = true;

        virtual bool IsAlive() { return Life > 0; }
        static bool IsAliveFn(const EffectBase& e) { return e.Life > 0; }

        // Called once per frame
        virtual void Update(float dt) { Life -= dt; }

        // Called per game tick
        virtual void FixedUpdate(float /*dt*/) { }

        virtual void Draw(Graphics::GraphicsContext&) {}
        virtual void DepthPrepass(Graphics::GraphicsContext&) {
            assert(IsTransparent); // must provide a depth prepass if not transparent
        }

        EffectBase() = default;
        virtual ~EffectBase() = default;
        EffectBase(const EffectBase&) = default;
        EffectBase(EffectBase&&) = default;
        EffectBase& operator=(const EffectBase&) = default;
        EffectBase& operator=(EffectBase&&) = default;
    };

    struct Particle final : EffectBase {
        VClipID Clip = VClipID::None;
        Vector3 Up = Vector3::Zero;
        Color Color = { 1, 1, 1 };
        float Radius = 1;
        float Rotation = 0;
        float FadeTime = 0; // How long it takes to fade the particle out
        float Delay = 0;
        bool RandomRotation = true;
        //float FadeDuration = 0;
        ObjID Parent = ObjID::None;
        Vector3 ParentOffset;

        void Update(float dt) override;
        void Draw(Graphics::GraphicsContext&) override;
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

        Particle CreateParticle() const {
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

    class ParticleEmitter final : public EffectBase {
        float _spawnTimer = 0; // internal timer for when to create a particle
        float _startDelay = 0;
        ParticleEmitterInfo _info;
        DataPool<Particle> _particles;
    public:
        ParticleEmitter(const ParticleEmitterInfo& info, size_t capacity)
            : _info(info), _particles([](auto& p) { return p.Life > 0; }, capacity) {
            _startDelay = info.StartDelay;
            Position = info.Position;
        }

        //span<const Particle> GetParticles() const { return _particles.GetLiveData(); }
        //void AddParticle() {
        //    _particles.Add(_info.CreateParticle());
        //}

        void Update(float dt) override;
        static bool IsAlive(const ParticleEmitter& p) { return p.Life > 0; }
    };

    //void AddEmitter(ParticleEmitter& emitter, size_t capacity);
    void AddParticle(Particle&, SegID);

    // Remains of a destroyed robot
    struct Debris final : EffectBase {
        Debris() { IsTransparent = false; }

        Matrix Transform, PrevTransform;
        Vector3 Velocity;
        Vector3 AngularVelocity;
        float Mass = 1;
        float Drag = 0.01f;
        float Radius = 1;
        ModelID Model = ModelID::None;
        int Submodel = 0;
        TexID TexOverride = TexID::None;

        void Draw(Graphics::GraphicsContext&) override;
        void DepthPrepass(Graphics::GraphicsContext&) override;
        void FixedUpdate(float dt) override;
    };

    void AddDebris(Debris&, SegID);

    struct ExplosionInfo {
        ObjID Parent = ObjID::None;
        VClipID Clip = VClipID::SmallExplosion;
        SoundID Sound = SoundID::None;
        float Volume = 1.0f;
        NumericRange<float> Radius = { 2.5f, 2.5f }; // size of the explosion
        float Variance = 0; // Position variance
        int Instances = 1; // how many explosions to create
        NumericRange<float> Delay = { 0.25f, 0.75f }; // how long to wait before creating the next explosion instance
        float InitialDelay = -1; // how long to wait before creating any explosions
        Color Color = { 1.75f, 1.75f, 1.75f }; // Particle color
        float FadeTime = 0; // How long it takes to fade the particles out
        SegID Segment = SegID::None;
        Vector3 Position;

        static bool IsAlive(const ExplosionInfo& info) {
            return info.InitialDelay >= 0;
        }
    };

    void CreateExplosion(ExplosionInfo&);
    //void UpdateExplosions(float dt);

    //enum class BeamFlag {
    //    SineNoise, RandomEnd
    //};

    struct BeamInfo {
        Vector3 Start;
        Vector3 End;
        ObjID StartObj = ObjID::None;
        int StartObjGunpoint = -1;
        ObjID EndObj = ObjID::None; // NYI: attaches beam to this object
        float Radius = 0; // If RandomEnd is true, randomly strike targets within this radius
        float Width = 2.0f;
        float Life = 0;
        Color Color = { 1, 1, 1 };
        float Noise = 0;
        string Texture;
        float ScrollSpeed = 0; // Texture scroll speed
        float Frequency = 1 / 60.0f; // How often in seconds to recalculate noise
        SegID Segment;
        float Scale = 4; // Scale for texture vs beam width
        bool SineNoise = false; // Sine noise when true, Fractal noise when false
        bool RandomEnd = false; // Uses a random end point
        float Time = 0; // animates noise and determines the phase
        float Amplitude = 0; // Peak to peak height of noise. 0 for straight beam.
        bool FadeEnd = false; // fades the start of the beam to 0 transparency
        bool FadeStart = false; // fades the end of the beam to 0 transparency

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

    struct TracerInfo final : EffectBase {
        ObjID Parent = ObjID::None; // Object the tracer is attached to. Normally a weapon projectile.
        ObjSig Signature = {};
        float Length = 20; // How long the tracer is
        float Width = 2;
        string Texture, BlobTexture;
        Color Color = { 1, 1, 1 };
        float FadeSpeed = 0.125f; // How quickly the tracer fades in and out

        // Runtime vars
        Vector3 End; // Updated in realtime. Used to fade out tracer after object dies.
        float Fade = 0; // For fading the tracer in and out
        bool ParentIsLive = false;
        static bool IsAlive(const TracerInfo& info) { return info.Life > 0; }

        void Update(float dt) override;
        void Draw(Graphics::GraphicsContext&) override;
    };

    // Adds a tracer effect attached to an object that is removed when the object dies.
    // Tracers are only drawn when the minimum length is reached
    void AddTracer(TracerInfo&, SegID);

    struct DecalInfo final : EffectBase {
        Vector3 Tangent, Bitangent;
        string Texture = "scorchB";

        float Radius = 2;
        Color Color = { 1, 1, 1 };
        SideID Side;
    };

    void AddDecal(DecalInfo& decal);
    void DrawDecals(Graphics::GraphicsContext& ctx);

    // Removes decals on a side
    void RemoveDecals(Tag);

    struct Spark {
        float Life = 0;
        Vector3 Velocity, PrevVelocity;
        Vector3 Position, PrevPosition;
        SegID Segment = SegID::None;
        static bool IsAlive(const Spark& s) { return s.Life > 0; }
    };

    class SparkEmitter final : public EffectBase {
        DataPool<Spark> _sparks = { Spark::IsAlive, 100 };
        bool _createdSparks = false;
    public:
        string Texture = "sun";
        Color Color = { 3.0, 3.0, 3.0 };
        float Width = 0.35f;

        NumericRange<float> Duration = { 1.0, 2.4f }; // Range for individual spark lifespans 
        NumericRange<uint> Count = { 80, 100 };
        NumericRange<float> Velocity = { 50, 75 };
        Vector3 Direction; // if Zero, random direction
        Vector3 Up; // Used with direction
        float ConeRadius = 1.0f; // Used with direction to spread sparks. Value of 1 is 45 degrees.
        float Drag = 0.02f;
        float FadeTime = 1.0f; // How long it takes to fade the particle out
        float Restitution = 0.8f; // How much velocity to keep after hitting a wall

        float VelocitySmear = 0.04f; // Percentage of velocity to add to spark length

        void FixedUpdate(float dt) override;
        void Draw(Graphics::GraphicsContext&) override;
    private:
        void CreateSpark();
    };

    void AddSparkEmitter(SparkEmitter&);

    void ResetParticles();

    span<Ptr<EffectBase>> GetEffectsInSegment(SegID);

    void InitEffects(const Level& level);
    void UpdateEffects(float dt);
    void FixedUpdateEffects(float dt);

    namespace Stats {
        inline uint EffectDraws = 0;
    }
}