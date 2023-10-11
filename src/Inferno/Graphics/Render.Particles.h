#pragma once

#include "DataPool.h"
#include "EffectClip.h"
#include "DirectX.h"
#include "Game.Object.h"
#include "Graphics/CommandContext.h"

namespace Inferno {
    struct Level;
}

namespace Inferno::Render {
    struct RenderCommand;
    float GetRenderDepth(const Vector3& pos);

    enum class RenderQueueType {
        None,
        Opaque,
        Transparent
    };

    struct EffectBase {
        SegID Segment = SegID::None;
        Vector3 Position, PrevPosition;
        float Duration = 0; // How long the effect lasts
        float Elapsed = 0; // How long the effect has been alive for
        RenderQueueType Queue = RenderQueueType::Transparent; // Which queue to render to
        float FadeTime = 0; // Fade time at the end of the effect's life
        float StartDelay = 0; // How long to wait in seconds before starting the effect
        ObjRef Parent;
        SubmodelRef ParentSubmodel;
        bool FadeOnParentDeath = false; // Detaches from the parent when it dies and uses FadeTime

        // Called once per frame
        void Update(float dt, EffectID id);

        // Called per game tick
        void FixedUpdate(float dt, EffectID);

        bool UpdatePositionFromParent();

        virtual void OnUpdate(float /*dt*/, EffectID) {}
        virtual void OnFixedUpdate(float /*dt*/, EffectID) {}

        virtual void Draw(Graphics::GraphicsContext&) {}

        virtual void DepthPrepass(Graphics::GraphicsContext&) {
            ASSERT(Queue == RenderQueueType::Transparent); // must provide a depth prepass if not transparent
        }

        virtual void OnExpire() {}
        virtual void OnInit() {}

        EffectBase() = default;
        virtual ~EffectBase() = default;
        EffectBase(const EffectBase&) = default;
        EffectBase(EffectBase&&) = default;
        EffectBase& operator=(const EffectBase&) = default;
        EffectBase& operator=(EffectBase&&) = default;
    };

    struct DynamicLight final : EffectBase {
        DynamicLight() { Queue = RenderQueueType::None; }

        DynamicLightMode Mode = DynamicLightMode::Constant;
        //float FlickerSpeed = 4.0f;
        //float FlickerRadius = 0;
        float Radius = -1; // Radius of emitted light
        Color LightColor; // Color of emitted light

        void OnUpdate(float dt, EffectID) override;

    private:
        Color _currentColor;
        float _currentRadius = 0;
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

        void Draw(Graphics::GraphicsContext&) override;
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
        ParticleEmitter(const ParticleEmitterInfo& info, size_t capacity)
            : _info(info), _particles([](auto& p) { return p.Elapsed < p.Duration; }, capacity) {
            StartDelay = info.StartDelay;
            Position = info.Position;
        }

        void OnUpdate(float dt, EffectID) override;
    };

    void AddParticle(Particle&, SegID, const Vector3& position);

    // Remains of a destroyed robot
    struct Debris final : EffectBase {
        Debris() { Queue = RenderQueueType::Opaque; }

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
        void OnFixedUpdate(float dt, EffectID) override;
        void OnExpire() override;
    };

    void AddDebris(Debris&, SegID);

    // An explosion can consist of multiple particles
    struct ExplosionInfo : EffectBase {
        ExplosionInfo() { Queue = RenderQueueType::None; }

        VClipID Clip = VClipID::SmallExplosion;
        SoundID Sound = SoundID::None;
        float Volume = 1.0f;
        NumericRange<float> Radius = { 2.5f, 2.5f }; // size of the explosion
        float Variance = 0; // Position variance
        int Instances = 1; // how many explosions to create
        NumericRange<float> Delay = { 0.25f, 0.75f }; // how long to wait before creating the next explosion instance
        float InitialDelay = -1; // how long to wait before creating any explosions
        Color LightColor = { 4.0f, 1.0f, 0.1f }; // Color of emitted light
        float LightRadius = 0;
        Color Color = { 2.75f, 2.25f, 2.25f }; // Particle color
        bool UseParentVertices = false; // Creates explosions on the parent vertices, offset from center using variance

        bool IsAlive() const { return InitialDelay >= 0; }
        void OnUpdate(float, EffectID) override;
    };

    void CreateExplosion(ExplosionInfo&, SegID, const Vector3& position);

    enum class BeamFlag {
        SineNoise = 1 << 0, // Sine noise when true, Fractal noise when false
        RandomEnd = 1 << 1, // Uses a random world end point
        FadeStart = 1 << 2, // fades the start of the beam to 0 transparency
        FadeEnd = 1 << 3, // fades the end of the beam to 0 transparency
        RandomObjStart = 1 << 4, // Uses a random start point on start object
        RandomObjEnd = 1 << 5, // Uses a random end point on start object
    };

    // An 'electric beam' connecting two points animated by noise
    struct BeamInfo {
        Vector3 Start; // Input: start of beam
        Vector3 End; // Input: end of beam
        ObjRef StartObj; // attaches start of beam to this object. Sets Start each update if valid.
        ObjRef EndObj; // attaches end of beam to this object. Sets End each update if valid
        SubmodelRef StartSubmodel, EndSubmodel;

        NumericRange<float> Radius; // If RandomEnd is true, randomly strike targets within this radius
        NumericRange<float> Width = { 2.0f, 2.0f };
        float Life = 0; // How long to live for
        float StartLife = 0; // How much life the beam started with (runtime variable)
        Color Color = { 1, 1, 1 };
        float Noise = 0;
        string Texture;
        float ScrollSpeed = 0; // Texture scroll speed in UV/second
        float Frequency = 1 / 60.0f; // How often in seconds to recalculate noise
        SegID Segment;
        float Scale = 4; // Scale for texture vs beam width
        float Time = 0; // animates noise and determines the phase
        float Amplitude = 0; // Peak to peak height of noise. 0 for straight beam.
        float StrikeTime = 1; // when using random end, how often to pick a new point
        float StartDelay = 0; // Delay in seconds before playing the effect
        float FadeInOutTime = 0;

        BeamFlag Flags{};
        bool HasRandomEndpoints() const {
            return HasFlag(Flags, BeamFlag::RandomEnd) || HasFlag(Flags, BeamFlag::RandomObjEnd) || HasFlag(Flags, BeamFlag::RandomObjStart);
        } 
        
        struct {
            float Length;
            int Segments;
            List<float> Noise;
            double NextUpdate;
            double NextStrikeTime;
            float Width;
            float OffsetU; // Random amount to offset the texture by
        } Runtime{};

        bool IsAlive() const { return Life > 0; }
    };

    void AddBeam(BeamInfo, float life, const Vector3& start, const Vector3& end);
    void AddBeam(BeamInfo, float life, ObjRef start, const Vector3& end, int startGun);
    void AddBeam(BeamInfo, float life, ObjRef start, ObjRef end = {}, int startGun = -1);

    void DrawBeams(Graphics::GraphicsContext& ctx);

    struct TracerInfo final : EffectBase {
        float Length = 20; // How long the tracer is
        float Width = 2;
        string Texture, BlobTexture;
        Color Color = { 1, 1, 1 };
        //float FadeSpeed = 0.2f; // How quickly the tracer fades in and out

        // Runtime vars
        //Vector3 End; // Updated in realtime. Used to fade out tracer after object dies.
        //float Fade = 0; // For fading the tracer in and out
        //bool ParentIsLive = false;
        Vector3 Direction; // Motion vector of the tracer
        float TravelDist = 0;
        //static bool IsAlive(const TracerInfo& info) { return info.Elapsed < info.Duration; }

        void OnUpdate(float dt, EffectID) override;
        void Draw(Graphics::GraphicsContext&) override;
    };

    // Adds a tracer effect attached to an object that is removed when the object dies.
    // Tracers are only drawn when the minimum length is reached
    void AddTracer(TracerInfo&, SegID, ObjRef parent);

    struct DecalInfo final : EffectBase {
        Vector3 Normal, Tangent, Bitangent;
        string Texture = "scorchB";

        float Radius = 2;
        float FadeRadius = 3.0; // Radius to grow to at end of life

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

    // Removes all effects associated with an object
    void RemoveEffects(ObjRef);

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
        SparkEmitter() { Queue = RenderQueueType::Transparent; }
        string Texture = "tracer";
        Color Color = { 3.0, 3.0, 3.0 };
        float Width = 0.35f;

        NumericRange<float> SparkDuration = { 1.0, 2.4f }; // Range for individual spark lifespans 
        NumericRange<uint> Count = { 80, 100 };
        NumericRange<float> Velocity = { 50, 75 };
        NumericRange<float> Interval = { 0, 0 }; // Interval between creating sparks. When zero, only creates sparks once.

        Vector3 Direction; // if Zero, random direction
        Vector3 Up; // Used with direction
        float ConeRadius = 1.0f; // Used with direction to spread sparks. Value of 1 is 45 degrees.
        float Drag = 0.02f;
        float Restitution = 0.8f; // How much velocity to keep after hitting a wall
        float SpawnRadius = 0; // Sphere to create new particles in
        float VelocitySmear = 0.04f; // Percentage of velocity to add to spark length
        bool UseWorldGravity = true; // Uses world gravity
        bool UsePointGravity = false; // Attracts sparks towards the center of the emitter
        bool FadeSize = false; // Reduces size to 0 at end of life
        Vector3 PointGravityOffset = Vector3::Zero; // Offset for the center of point gravity
        Vector3 Offset; // Offset when creating particles. Uses relative rotations if has a parent.
        Vector3 PointGravityVelocity = Vector3::Zero; // Applies a gravity field relative to the parent object rotation
        float PointGravityStrength = 0;
        Vector3 PrevParentPosition;
        bool Relative = false; // Particles move relative to parent when updating instead of detaching into the world
        bool Physics = true; // Collides with world geometry

        void OnInit() override;
        void OnUpdate(float dt, EffectID) override;
        void OnFixedUpdate(float dt, EffectID) override;
        void Draw(Graphics::GraphicsContext&) override;

    private:
        void CreateSpark();
    };

    //void AddSparkEmitter(SparkEmitter&);
    void AddSparkEmitter(SparkEmitter, SegID, const Vector3& worldPos = Vector3::Zero);
    EffectID AddDynamicLight(DynamicLight&);

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

    // Stores default effects
    class EffectLibrary {
        // Create a copy of the effect so local changes aren't saved
        template <class T>
        Option<T> MaybeCopyValue(Dictionary<string, T>& data, const string& name) {
            if (name.empty()) return {};
            if (auto value = TryGetValue(data, name)) return *value;
            return {};
        }

    public:
        Dictionary<string, BeamInfo> Beams;
        Dictionary<string, ExplosionInfo> Explosions;
        Dictionary<string, SparkEmitter> Sparks;
        Dictionary<string, TracerInfo> Tracers;

        Option<BeamInfo> GetBeamInfo(const string& name) { return MaybeCopyValue(Beams, name); }
        Option<ExplosionInfo> GetExplosion(const string& name) { return MaybeCopyValue(Explosions, name); }
        Option<SparkEmitter> GetSparks(const string& name) { return MaybeCopyValue(Sparks, name); }
        Option<TracerInfo> GetTracer(const string& name) { return MaybeCopyValue(Tracers, name); }

        //void LoadEffects(const filesystem::path& path);
    };

    inline class EffectLibrary EffectLibrary;
}
