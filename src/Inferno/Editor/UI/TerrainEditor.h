#pragma once

#include "Game.h"
#include "Settings.h"
#include "WindowBase.h"

namespace Inferno::Editor {
    class TerrainEditor : public WindowBase {
        bool _randomSeed = true;

    public:
        TerrainEditor() : WindowBase("Terrain Editor", &Settings::Editor.Windows.TerrainEditor) {}

    protected:
        void OnUpdate() override {
            bool changed = false;

            auto& args = TerrainGenInfo; // Use state of current level
            changed |= ImGui::DragFloat("Height", &args.Height, 1, -512, 512, "%.2f");
            changed |= ImGui::DragFloat("Noise", &args.NoiseScale, 0.01f, 0.01f, 0, "%.2f");

            changed |= ImGui::DragFloat("Height 2", &args.Height2, 1, -512, 512, "%.2f");
            changed |= ImGui::DragFloat("Noise 2", &args.NoiseScale2, 0.01f, 0.01f, 0, "%.2f");

            ImGui::Separator();

            changed |= ImGui::DragFloat("Size", &args.Size, 1, 1);
            changed |= ImGui::DragFloat("Flatten radius", &args.FlattenRadius, 1, 0);
            changed |= ImGui::DragFloat("Front flatten radius", &args.FrontFlattenRadius, 1, 0);
            changed |= ImGui::DragFloat("Crater", &args.CraterStrength, 1, 0);

            constexpr uint64 step = 1;
            changed |= ImGui::InputScalar("Detail", ImGuiDataType_U32, &args.Density, &step, &step);

            changed |= ImGui::DragFloat("Texture scale", &args.TextureScale, 1, 1);

            changed |= ImGui::InputScalar("Seed", ImGuiDataType_U64, &args.Seed, &step, &step);
            ImGui::Separator();


            ImGui::ColorEdit3("Atmosphere", &Game::Terrain.AtmosphereColor.x);
            ImGui::ColorEdit3("Ambient", &Game::Terrain.Light.x);
            ImGui::ColorEdit3("Star color", &Game::Terrain.StarColor.x);

            ImGui::Separator();

            if (ImGui::Button("Reset")) {
                args = {};
                changed = true;
            }

            //if (ImGui::Button("Random seed")) {
            //    _seed = Inferno::RandomInt(std::numeric_limits<int>::max());
            //    changed = true;
            //}

            if (changed) {
                //if (ImGui::Button("Generate")) {
                args.Density = std::clamp(args.Density, 16u, 64u);
                if (args.FlattenRadius < 0) args.FlattenRadius = 0;
                GenerateTerrain(Game::Terrain, args);
                Render::TerrainChanged = true;
            }
        }
    };
}
