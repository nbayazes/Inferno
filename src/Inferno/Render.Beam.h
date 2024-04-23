#pragma once

#include "VisualEffects.h"
#include "Types.h"
#include "Render.Effect.h"

namespace Inferno::Render {
    // An 'electric beam' connecting two points animated by noise
    struct BeamInstance final : EffectBase  {
        BeamInfo Info;

        Vector3 Start; // Input: start of beam
        Vector3 End; // Input: end of beam
        //ObjRef StartObj; // attaches start of beam to this object. Sets Start each update if valid.
        ObjRef EndObj; // attaches end of beam to this object. Sets End each update if valid
        SubmodelRef EndSubmodel;
        double Time = 0; // animates noise and determines the phase

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

        void Draw(GraphicsContext&) override;

        void InitRandomPoints(const Object* object);
    };
}