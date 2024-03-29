#pragma once

#include "EffectClip.h"
#include "DirectX.h"

namespace Inferno::Render {
    struct Particle {
        VClipID Clip = VClipID::None;
        Vector3 Position;
        Vector3 Up = Vector3::Zero;
        Color Color = { 1, 1, 1 };
        float Radius = 1;
        float Rotation = 0;
        float Life = 0;

        static bool IsAlive(const Particle& p) { return p.Life > 0; }
    };

    void AddParticle(Particle&, bool randomRotation = true);

    void UpdateParticles(float dt);
    void DrawParticles(ID3D12GraphicsCommandList* cmd);
}