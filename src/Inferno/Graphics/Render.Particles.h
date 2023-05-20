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
        float Duration = 0; // How long the effect lasts
        float Elapsed = 0;  // How long the effect has been alive for
        bool IsTransparent = true;
        float LightRadius = 0; // Radius of emitted light
        Color LightColor;      // Color of emitted light
        float FadeTime = 0;    // Fade time at the end of the particle's life

        virtual bool IsAlive() { return Elapsed < Duration; }

        // Called once per frame
        virtual void Update(float dt) { Elapsed += dt; }

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
        float Life = 0;             // How long the emitter lives for
        ObjID Parent = ObjID::None; // Moves with this object
        Vector3 ParentOffset;       // Offset from parent
        Vector3 Position;
        Vector3 Velocity;

        //SegID Segment = SegID::None; // For sorting
        Color Color = { 1, 1, 1 };
        float Variance = 0;
        bool RandomRotation = true;
        int ParticlesToSpawn = 1;         // stops creating particles once this reaches zero. -1 to create particles forever
        float StartDelay = 0;             // How long to wait before emitting particles
        float MinDelay = 0, MaxDelay = 0; // How often to spawn a particle
        float MinRadius = 1, MaxRadius = 2;

        Particle CreateParticle() const {
            auto& vclip = Resources::GetVideoClip(Clip);

            Particle p;
            p.Color = Color;
            p.Clip = Clip;
            p.Duration = vclip.PlayTime;
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
            : _info(info), _particles([](auto& p) { return p.Elapsed < p.Duration; }, capacity) {
            _startDelay = info.StartDelay;
            Position = info.Position;
        }

        //span<const Particle> GetParticles() const { return _particles.GetLiveData(); }
        //void AddParticle() {
        //    _particles.Add(_info.CreateParticle());
        //}

        void Update(float dt) override;
        bool IsAlive() const { return Elapsed < Duration; }
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

    // An explosion can consist of multiple particles
    struct ExplosionInfo {
        ObjID Parent = ObjID::None;
        VClipID Clip = VClipID::SmallExplosion;
        SoundID Sound = SoundID::None;
        float Volume = 1.0f;
        NumericRange<float> Radius = { 2.5f, 2.5f };  // size of the explosion
        float Variance = 0;                           // Position variance
        int Instances = 1;                            // how many explosions to create
        NumericRange<float> Delay = { 0.25f, 0.75f }; // how long to wait before creating the next explosion instance
        float InitialDelay = -1;                      // how long to wait before creating any explosions
        Color LightColor = { 4.0f, 1.0f, 0.1f };      // Color of emitted light
        Color Color = { 2.75f, 2.25f, 2.25f };        // Particle color
        float FadeTime = 0;                           // How long it takes to fade the particles out
        SegID Segment = SegID::None;
        Vector3 Position;

        bool IsAlive() const { return InitialDelay >= 0; }
    };

    void CreateExplosion(ExplosionInfo&);

    enum class BeamFlag {
        SineNoise = 1 << 0, // Sine noise when true, Fractal noise when false
        RandomEnd = 1 << 1, // Uses a random end point
        FadeStart = 1 << 2, // fades the start of the beam to 0 transparency
        FadeEnd = 1 << 3    // fades the end of the beam to 0 transparency
    };

    // An 'electric beam' connecting two points animated by noise
    struct BeamInfo {
        Vector3 Start;                // Input: start of beam
        Vector3 End;                  // Input: end of beam
        ObjID StartObj = ObjID::None; // attaches start of beam to this object. Sets Start each update if valid.
        int StartObjGunpoint = -1;    // Gunpoint of StartObj to attach the beam to
        ObjID EndObj = ObjID::None;   // attaches end of beam to this object. Sets End each update if valid

        NumericRange<float> Radius; // If RandomEnd is true, randomly strike targets within this radius
        NumericRange<float> Width = { 2.0f, 2.0f };
        float Life = 0;
        Color Color = { 1, 1, 1 };
        float Noise = 0;
        string Texture;
        float ScrollSpeed = 0;       // Texture scroll speed in UV/second
        float Frequency = 1 / 60.0f; // How often in seconds to recalculate noise
        SegID Segment;
        float Scale = 4;      // Scale for texture vs beam width
        float Time = 0;       // animates noise and determines the phase
        float Amplitude = 0;  // Peak to peak height of noise. 0 for straight beam.
        float StrikeTime = 1; // when using random end, how often to pick a new point

        BeamFlag Flags{};
        // Flags
        //bool SineNoise = false; // Sine noise when true, Fractal noise when false
        //bool RandomEnd = false; // Uses a random end point
        //bool FadeEnd = false;   // fades the start of the beam to 0 transparency
        //bool FadeStart = false; // fades the end of the beam to 0 transparency

        struct {
            float Length;
            int Segments;
            List<float> Noise;
            float NextUpdate;
            float NextStrikeTime;
            float Width;
            float OffsetU; // Random amount to offset the texture by
        } Runtime{};

        bool IsAlive() const { return Life > 0; }
    };

    void AddBeam(const string& effect, float life, const Vector3& start, const Vector3& end);
    void AddBeam(const string& effect, float life, ObjID start, const Vector3& end, int startGun = -1);
    void AddBeam(const string& effect, float life, ObjID start, ObjID end = ObjID::None, int startGun = -1);

    void DrawBeams(Graphics::GraphicsContext& ctx);

    struct TracerInfo final : EffectBase {
        ObjID Parent = ObjID::None; // Object the tracer is attached to. Normally a weapon projectile.
        ObjSig Signature = {};
        float Length = 20; // How long the tracer is
        float Width = 2;
        string Texture, BlobTexture;
        Color Color = { 1, 1, 1 };
        float FadeSpeed = 0.2f; // How quickly the tracer fades in and out

        // Runtime vars
        Vector3 End;    // Updated in realtime. Used to fade out tracer after object dies.
        float Fade = 0; // For fading the tracer in and out
        bool ParentIsLive = false;
        //static bool IsAlive(const TracerInfo& info) { return info.Elapsed < info.Duration; }

        void Update(float dt) override;
        void Draw(Graphics::GraphicsContext&) override;
    };

    // Adds a tracer effect attached to an object that is removed when the object dies.
    // Tracers are only drawn when the minimum length is reached
    void AddTracer(TracerInfo&, SegID);

    struct DecalInfo final : EffectBase {
        Vector3 Normal, Tangent, Bitangent;
        string Texture = "scorchB";

        float Radius = 2;
        float FadeRadius = 3.0; // Radius to fade to

        Color Color = { 1, 1, 1 };
        SideID Side;
        bool Additive = false;
    };

    void AddDecal(DecalInfo& decal);
    void DrawDecals(Graphics::GraphicsContext& ctx, float dt);
    span<DecalInfo> GetAdditiveDecals();
    span<DecalInfo> GetDecals();

    // Removes decals on a side
    void RemoveDecals(Tag);

    struct Spark {
        float Life = 0;
        Vector3 Velocity, PrevVelocity;
        Vector3 Position, PrevPosition;
        SegID Segment = SegID::None;
        bool IsAlive() const { return Life > 0; }
    };

    class SparkEmitter final : public EffectBase {
        DataPool<Spark> _sparks = { &Spark::IsAlive, 100 };
        bool _createdSparks = false;

    public:
        string Texture = "sun";
        Color Color = { 3.0, 3.0, 3.0 };
        float Width = 0.35f;

        NumericRange<float> DurationRange = { 1.0, 2.4f }; // Range for individual spark lifespans 
        NumericRange<uint> Count = { 80, 100 };
        NumericRange<float> Velocity = { 50, 75 };
        Vector3 Direction;       // if Zero, random direction
        Vector3 Up;              // Used with direction
        float ConeRadius = 1.0f; // Used with direction to spread sparks. Value of 1 is 45 degrees.
        float Drag = 0.02f;
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

    // Stores default effects
    class EffectLibrary {
        Dictionary<string, TracerInfo> _tracers;
        Dictionary<string, ParticleEmitterInfo> _particleEmitter;
        Dictionary<string, ExplosionInfo> _explosions;
        Dictionary<string, SparkEmitter> _sparks;

    public:
        Dictionary<string, BeamInfo> Beams;
        BeamInfo* GetBeamInfo(const string& name) { return TryGetValue(Beams, name); }

        void ApplyEffect(TracerInfo& info, const string& name);
        void ApplyEffect(ParticleEmitterInfo& info, const string& name);
        void ApplyEffect(ExplosionInfo& info, const string& name);
        void ApplyEffect(SparkEmitter& info, const string& name);

        //void LoadEffects(const filesystem::path& path);
    };

    inline EffectLibrary DefaultEffects;
}
