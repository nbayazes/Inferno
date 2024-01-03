#pragma once

#include "Types.h"
#include "Render.Effect.h"

namespace Inferno::Render {

    enum class BeamFlag {
        SineNoise = 1 << 0, // Sine noise when true, Fractal noise when false
        RandomEnd = 1 << 1, // Uses a random world end point
        FadeStart = 1 << 2, // fades the start of the beam to 0 transparency
        FadeEnd = 1 << 3, // fades the end of the beam to 0 transparency
        RandomObjStart = 1 << 4, // Uses a random start point on start object
        RandomObjEnd = 1 << 5, // Uses a random end point on start object
    };

    // An 'electric beam' connecting two points animated by noise
    struct BeamInfo final : EffectBase  {
        Vector3 Start; // Input: start of beam
        Vector3 End; // Input: end of beam
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
        float Time = 0; // animates noise and determines the phase
        float Amplitude = 0; // Peak to peak height of noise. 0 for straight beam.
        float StrikeTime = 1; // when using random end, how often to pick a new point
        float StartDelay = 0; // Delay in seconds before playing the effect
        float FadeInOutTime = 0; // Fades in and out using this delay

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

        //bool IsAlive() const { return Life > 0; }

        void Draw(Graphics::GraphicsContext&) override;

    };

    void InitRandomBeamPoints(BeamInfo& beam, const Object* object);

    void AddBeam(BeamInfo, float life, const Vector3& start, const Vector3& end);
    void AddBeam(BeamInfo, float life, ObjRef start, const Vector3& end, int startGun);
    void AddBeam(BeamInfo, float duration, ObjRef start, ObjRef end = {}, int startGun = -1);
}