#pragma once

#include "WindowBase.h"
#include "Graphics/Render.h"
#include "Settings.h"

namespace Inferno::Editor {
    class BloomWindow : public WindowBase {
    public:
        BloomWindow() : WindowBase("Bloom") {}

    protected:
        void OnUpdate() override {
            DisableControls disable(!Settings::Graphics.EnableBloom);
            ImGui::SliderFloat("Bloom Exposure", &Render::ToneMapping->BloomExtractDownsample.Exposure, 0.1f, 5.0f);
            ImGui::SliderFloat("Bloom Threshold", &Render::ToneMapping->BloomExtractDownsample.BloomThreshold, 0.0f, 3.0f);

            ImGui::SliderFloat("Blur Factor", &Render::ToneMapping->Upsample.UpsampleBlendFactor, 0.0f, 1.0f);

            ImGui::SliderFloat("Bloom Strength", &Render::ToneMapping->ToneMap.BloomStrength, 0.0f, 5.0f);
            ImGui::SliderFloat("Tone Map Exposure", &Render::ToneMapping->ToneMap.Exposure, 0.0f, 3.0f);
            ImGui::Checkbox("Debug Emissive", &Render::DebugEmissive);
        }
    };
}