#pragma once

#include "imgui_local.h"
#include "Game.h"
#include "Graphics/Render.h"

namespace Inferno::Editor {
    inline void DrawDebugOverlay(const ImGuiDockNode* node) {
        ImVec2 window_pos = ImVec2(node->Pos.x + node->Size.x, node->Pos.y + 40);
        ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        ImGui::SetNextWindowViewport(node->ID);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0, 0, 0, 0.5f });

        //ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("Debug Overlay", nullptr, window_flags)) {
            static Array<float, 90> values = {};
            static int values_offset = 0;
            static double refresh_time = 0.0;

            if (refresh_time == 0.0)
                refresh_time = Game::ElapsedTime;

            while (refresh_time < Game::ElapsedTime) // Create dummy data at fixed 60 Hz rate for the demo
            {
                values[values_offset] = Render::FrameTime;
                values_offset = (values_offset + 1) % values.size();
                refresh_time += 1.0f / 60.0f;
            }

            {
                float average = 0.0f;
                for (int n = 0; n < values.size(); n++)
                    average += values[n];
                average /= (float)values.size();
                auto overlay = fmt::format("FPS {:.1f} ({:.2f} ms)", 1 / average, average * 1000);
                ImGui::PlotLines("##FrameTime", values.data(), (int)values.size(), values_offset, overlay.c_str(), 0, 1 / 20.0f, ImVec2(0, 120.0f));
            }
        }
        ImGui::End();

        ImGui::PopStyleColor();
    }
}
