#pragma once
#include "Game.Object.h"
#include "Utility.h"

namespace Inferno {
    enum class BeamFlag {
        SineNoise = 1 << 0, // Sine noise when true, Fractal noise when false
        RandomEnd = 1 << 1, // Uses a random world end point
        FadeStart = 1 << 2, // fades the start of the beam to 0 transparency
        FadeEnd = 1 << 3, // fades the end of the beam to 0 transparency
        RandomObjStart = 1 << 4, // Uses a random start point on start object
        RandomObjEnd = 1 << 5, // Uses a random end point on start object
    };

    struct BeamInfo {
        float Duration = 1;
        //ObjRef StartObj; // attaches start of beam to this object. Sets Start each update if valid.
        ObjRef EndObj; // attaches end of beam to this object. Sets End each update if valid
        SubmodelRef EndSubmodel;

        NumericRange<float> Radius; // If RandomEnd is true, randomly strike targets within this radius
        NumericRange<float> Width = { 2.0f, 2.0f };
        //float Life = 0; // How long to live for
        //float StartLife = 0; // How much life the beam started with (runtime variable)
        Color Color = { 1, 1, 1 };
        //float Noise = 0;
        string Texture;
        float ScrollSpeed = 0; // Texture scroll speed in UV/second
        float Frequency = 1 / 60.0f; // How often in seconds to recalculate noise
        float Scale = 4; // Scale for texture vs beam width
        float Amplitude = 0; // Peak to peak height of noise. 0 for straight beam.
        float StrikeTime = 1; // when using random end, how often to pick a new point
        float StartDelay = 0; // Delay in seconds before playing the effect
        float FadeInOutTime = 0; // Fades in and out using this delay
        BeamFlag Flags{};

        bool HasRandomEndpoints() const {
            return HasFlag(Flags, BeamFlag::RandomEnd) || HasFlag(Flags, BeamFlag::RandomObjEnd) || HasFlag(Flags, BeamFlag::RandomObjStart);
        }
    };

    struct DebrisInfo {
        float Mass = 1;
        float Drag = 0.03f;
        float Radius = 1;
        ModelID Model = ModelID::None;
        int Submodel = 0;
        TexID TexOverride = TexID::None;
    };

    struct ParticleInfo {
        float FadeTime = 0;
        VClipID Clip = VClipID::None;
        Vector3 Up = Vector3::Zero;
        Color Color = { 1, 1, 1 };
        float Radius = 1;
        float Rotation = 0;
        float Delay = 0;
        bool RandomRotation = true;
    };

    struct LightEffectInfo {
        float FadeTime = 0;
        DynamicLightMode Mode = DynamicLightMode::Constant;
        bool FadeOnParentDeath = false;
        //float FlickerSpeed = 4.0f;
        //float FlickerRadius = 0;
        float Radius = -1; // Radius of emitted light
        Color LightColor; // Color of emitted light
        float SpriteMult = 1; // Multiplier when applying to sprites and the player hud
    };

    struct SparkEmitterInfo {
        float FadeTime = 0;
        string Texture = "tracer";
        Color Color = { 3.0, 3.0, 3.0 };
        float Width = 0.35f;

        NumericRange<float> Duration = { 1.0, 2.4f }; // Range for individual spark lifespans 
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
        bool Relative = false; // Particles move relative to parent when updating instead of detaching into the world
        bool Physics = false; // Collides with world geometry
    };

    struct ExplosionEffectInfo {
        float FadeTime = 0;
        VClipID Clip = VClipID::SmallExplosion;
        SoundID Sound = SoundID::None;
        float Volume = 1.0f;
        NumericRange<float> Radius = { 2.5f, 2.5f }; // size of the explosion
        float Variance = 0; // Position variance
        int Instances = 1; // how many explosions to create
        NumericRange<float> Delay = { 0.25f, 0.75f }; // how long to wait before creating the next explosion instance
        Color LightColor = { 4.0f, 1.0f, 0.1f }; // Color of emitted light
        float LightRadius = 0;
        Color Color = { 2.75f, 2.25f, 2.25f }; // Particle color
        bool UseParentVertices = false; // Creates explosions on the parent vertices, offset from center using variance
    };

    struct TracerInfo {
        float FadeTime = 0;
        float Duration = 1;
        float Length = 20; // How long the tracer is
        float Width = 2;
        string Texture, BlobTexture;
        Color Color = { 1, 1, 1 };
        //float FadeSpeed = 0.2f; // How quickly the tracer fades in and out
    };

    struct Decal {
        float FadeTime = 0;
        float FadeRadius = 3.0; // Radius to grow to at end of life
        string Texture = "scorchB";
        float Radius = 2;
        Color Color = { 1, 1, 1 };
        bool Additive = false;
    };

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
        Dictionary<string, ExplosionEffectInfo> Explosions;
        Dictionary<string, SparkEmitterInfo> Sparks;
        Dictionary<string, TracerInfo> Tracers;

        Option<BeamInfo> GetBeamInfo(const string& name) { return MaybeCopyValue(Beams, name); }
        Option<ExplosionEffectInfo> GetExplosion(const string& name) { return MaybeCopyValue(Explosions, name); }
        Option<SparkEmitterInfo> GetSparks(const string& name) { return MaybeCopyValue(Sparks, name); }
        Option<TracerInfo> GetTracer(const string& name) { return MaybeCopyValue(Tracers, name); }

        //void LoadEffects(const filesystem::path& path);
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

        //Particle CreateParticle() const;
    };

    // Adds a tracer effect attached to an object that is removed when the parent object dies.
    // Tracers are only drawn when the minimum length is reached
    void AddTracer(const TracerInfo&, ObjRef parent);

    //void AddSparkEmitter(SparkEmitter&);
    //void AddDecal(Decal& decal, Tag tag, const Vector3& position);
    inline class EffectLibrary EffectLibrary;

    void AddDecal(const Decal& info, Tag tag, const Vector3& position, const Vector3& normal, const Vector3& tangent, float duration);
    void AddBeam(const BeamInfo&, SegID seg, float duration, const Vector3& start, const Vector3& end);
    void AddBeam(const BeamInfo&, float duration, ObjRef start, const Vector3& end, int startGun);
    void AttachBeam(const BeamInfo&, float duration, ObjRef start, ObjRef end = {}, int startGun = -1);

    void AddParticle(const ParticleInfo&, SegID, const Vector3& position);
    void AttachParticle(const ParticleInfo&, ObjRef parent, SubmodelRef submodel = {});

    void AddSparkEmitter(const SparkEmitterInfo&, SegID, const Vector3& worldPos = Vector3::Zero);
    void AttachSparkEmitter(const SparkEmitterInfo&, ObjRef parent, const Vector3& offset = Vector3::Zero);

    EffectID AddLight(const LightEffectInfo& info, const Vector3& position, float duration, SegID segment);
    EffectID AttachLight(const LightEffectInfo& info, ObjRef parent, SubmodelRef submodel = {});

    void AddDebris(const DebrisInfo& info, const Matrix& transform, SegID seg, const Vector3& velocity, const Vector3& angularVelocity, float duration);

    void CreateExplosion(ExplosionEffectInfo&, SegID, const Vector3& position, float duration = 0, float startDelay = 0);
    void CreateExplosion(ExplosionEffectInfo&, ObjRef parent, float duration = 0, float startDelay = 0);

    // Removes decals on a side
    void RemoveDecals(Tag);

    // Removes all effects associated with an object
    void RemoveEffects(ObjRef);

    // Detach effects from an object and cause them to fade out
    void DetachEffects(ObjRef);

    void StopEffect(EffectID);

    // Clears all effects
    void ResetEffects();

    void FixedUpdateEffects(float dt);
}
