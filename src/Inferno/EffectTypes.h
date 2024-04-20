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
        float Duration = 1;
        float FadeTime = 0;
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

    inline class EffectLibrary EffectLibrary;
}
