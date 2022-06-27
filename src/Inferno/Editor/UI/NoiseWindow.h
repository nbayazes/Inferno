#pragma once

#include "WindowBase.h"
#include "Graphics/Render.h"
#include "../Editor.Geometry.h"

namespace Inferno::Editor {
    class NoiseWindow : public WindowBase {
        Vector3 _strength = { 2.5f, 2.5f, 2.5f };
        float _scale = 20.0;
        int _seed = 0;
        bool _randomSeed = true;
    public:
        NoiseWindow() : WindowBase("Noise", &Settings::Windows.Noise) {}

    protected:
        void OnUpdate() override {
            ImGui::DragFloat3("Strength", &_strength.x, 1, 0, 100, "%.2f");
            ImGui::HelpMarker("Maximum amount of movement on each axis");
            
            ImGui::DragFloat("Scale", &_scale, 1.0f, 1, 1000, "%.2f", 2);
            ImGui::HelpMarker("A higher scale creates larger waves\nwith less localized noise");
            
            ImGui::DragInt("Seed", &_seed);
            ImGui::Checkbox("Random Seed", &_randomSeed);

            if (ImGui::Button("Apply", { 100, 0 })) {
                Commands::ApplyNoise(_scale, _strength, _seed);
                if (_randomSeed) _seed = rand();
            }
        }
    };
}