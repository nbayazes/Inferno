#pragma once

#include "Game.h"
#include "Resources.h"
#include "Settings.h"
#include "WindowBase.h"

namespace Inferno::Editor {
    class TerrainEditor : public WindowBase {
        bool _randomSeed = true;
        TerrainGenerationInfo _args;

    public:
        TerrainEditor() : WindowBase("Terrain Editor", &Settings::Editor.Windows.TerrainEditor) {}

    protected:
        void OnUpdate() override {
            bool changed = false;

            changed |= ImGui::DragFloat("Height", &_args.Height, 1, -512, 512, "%.2f");
            changed |= ImGui::DragFloat("Noise", &_args.NoiseScale, 0.01f, 0.01f, 0, "%.2f");

            changed |= ImGui::DragFloat("Height 2", &_args.Height2, 1, -512, 512, "%.2f");
            changed |= ImGui::DragFloat("Noise 2", &_args.NoiseScale2, 0.01f, 0.01f, 0, "%.2f");

            ImGui::Separator();

            changed |= ImGui::DragFloat("Size", &_args.Size, 1, 1);
            changed |= ImGui::DragFloat("Flatten radius", &_args.FlattenRadius, 1, 0);
            changed |= ImGui::DragFloat("Crater", &_args.CraterStrength, 1, 0);

            constexpr uint64 step = 1;
            changed |= ImGui::InputScalar("Detail", ImGuiDataType_U32, &_args.Density, &step, &step);

            changed |= ImGui::DragFloat("Texture scale", &_args.TextureScale, 1, 1);

            changed |= ImGui::InputScalar("Seed", ImGuiDataType_U64, &_args.Seed, &step, &step);
            ImGui::Separator();

            if (ImGui::Button("Reset")) {
                _args = {};
                changed = true;
            }

            //if (ImGui::Button("Random seed")) {
            //    _seed = Inferno::RandomInt(std::numeric_limits<int>::max());
            //    changed = true;
            //}


            if (changed) {
                //if (ImGui::Button("Generate")) {
                _args.Density = std::clamp(_args.Density, 16u, 64u);
                if (_args.FlattenRadius < 0) _args.FlattenRadius = 0;
                GenerateTerrain(Game::Terrain, _args);
                Render::TerrainChanged = true;
            }
        }
    };
}
