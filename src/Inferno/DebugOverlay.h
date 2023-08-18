#pragma once

#include "imgui_local.h"
#include "Game.h"
#include "Physics.h"
#include "Graphics/Render.h"
#include "Graphics/Render.Particles.h"
#include "Graphics/Render.Level.h"

namespace Inferno {
    // Performance overlay
    inline void DrawDebugOverlay(const ImVec2& pos, const ImVec2& pivot) {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, { 0, 0, 0, 0.5f });

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("Debug Overlay", nullptr, flags)) {
            static Array<float, 90> values = {};
            static int values_offset = 0;
            static double refresh_time = 0.0;
            static int usedValues = 0;

            if (refresh_time == 0.0)
                refresh_time = Render::ElapsedTime;

            while (refresh_time < Render::ElapsedTime) {
                values[values_offset] = Render::FrameTime;
                values_offset = (values_offset + 1) % values.size();
                refresh_time += 1.0f / 60.0f;
                usedValues = std::min((int)values.size(), usedValues + 1);
            }

            {
                float average = 0.0f;
                for (int n = 0; n < usedValues; n++)
                    average += values[n];

                if (usedValues > 0) average /= (float)usedValues;
                auto overlay = fmt::format("FPS {:.1f} ({:.2f} ms)  Calls: {:d}", 1 / average, average * 1000, Render::Stats::DrawCalls);
                ImGui::PlotLines("##FrameTime", values.data(), (int)values.size(), values_offset, overlay.c_str(), 0, 1 / 20.0f, ImVec2(0, 120.0f));
            }
        }
        ImGui::End();

        ImGui::PopStyleColor();
    }

    // Player ship info, rooms, AI, etc
    inline void DrawGameDebugOverlay(const ImVec2& pos, const ImVec2& pivot) {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::PushStyleColor(ImGuiCol_Border, { 0, 0, 0, 0 });

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("Game Debug Overlay", nullptr, flags)) {
            if (auto player = Game::Level.TryGetObject(ObjID(0))) {
                if (auto seg = Game::Level.TryGetSegment(player->Segment)) {
                    bool hasLava = bool(seg->AmbientSound & SoundFlag::AmbientLava);
                    bool hasWater = bool(seg->AmbientSound & SoundFlag::AmbientWater);
                    ImGui::Text("Segment: %d Type: %d", player->Segment, seg->Type);
                    string type = hasLava ? "Lava" : (hasWater ? "Water" : "Normal");
                    ImGui::Text("Room type: %s", type.c_str());
                    ImGui::Text("Seg Effects: %d", seg->Effects.size());
                }
                ImGui::Text("Ship vel: %.2f", Debug::ShipVelocity.Length());
                ImGui::Text("Ship thrust: %.2f", Debug::ShipThrust.Length());
            }

            ImGui::Text("Objects: %d", Game::Level.Objects.size());
            ImGui::Text("Segments: %d", Render::Stats::VisitedSegments);
            //ImGui::Text("Total Effects: %d", Render::Stats::EffectDraws);
            ImGui::Text("Queue Size (T): %d", Render::GetTransparentQueueSize());
            ImGui::Text("Collision segs: %d", Debug::SegmentsChecked);
            ImGui::Text("Dynamic Lights: %d", Graphics::Lights.GetCount());
        }
        ImGui::End();

        ImGui::PopStyleColor();
    }
}
