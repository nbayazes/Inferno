#pragma once

#include "WindowBase.h"
#include "Graphics/Render.h"
#include "../Editor.Lighting.h"
#include "Game.Segment.h"

namespace Inferno::Editor {
    class LightingWindow final : public WindowBase {
    public:
        LightingWindow() : WindowBase("Lighting", &Settings::Editor.Windows.Lighting) {}

    protected:
        static void BreakLight() {
            if (Editor::Selection.Segment == SegID::None) return;

            auto pSeg = Game::Level.TryGetSegment(Editor::Selection.Segment);
            if (!pSeg) return;
            auto& seg = *pSeg;
            auto [tmap1, tmap2] = seg.GetTexturesForSide(Editor::Selection.Side);
            if (tmap2 <= LevelTexID(0)) return;
            auto destroyedTex = Resources::GetDestroyedTexture(tmap2);
            if (destroyedTex == LevelTexID::None) return;

            if (ImGui::Button("Break light")) {
                seg.GetSide(Editor::Selection.Side).TMap2 = destroyedTex;

                Inferno::SubtractLight(Game::Level, Editor::Selection.Tag(), seg);
                Events::LevelChanged();
                //Render::LoadTextureDynamic(destroyedTex);
            }
        }

        void ToggleLight() const {
            if (!Game::Level.SegmentExists(Editor::Selection.Segment)) return;

            auto& side = Game::Level.GetSide(Editor::Selection.Tag());
            if (Resources::GetLevelTextureInfo(side.TMap).Lighting == 0 &&
                Resources::GetLevelTextureInfo(side.TMap2).Lighting == 0)
                return;

            if (ImGui::Button("Toggle light")) {
                Inferno::ToggleLight(Game::Level, Editor::Selection.Tag());
                Events::LevelChanged();
            }
        }

        void OnUpdate() override {
            auto& settings = Settings::Editor.Lighting;
            ImGui::ColorEdit3("Ambient", &settings.Ambient.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
            ImGui::SliderFloat("Multiplier", &settings.Multiplier, 0, 4);
            //ImGui::SliderFloat("Distance Threshold", &_settings.DistanceThreshold, 60, 300);
            //ImGui::SliderFloat("Falloff", &_settings.Attenuation, 0, 8);
            ImGui::SliderFloat("Falloff", &settings.Falloff, 0.02f, 0.2f);
            //auto sz = ImGui::GetItemRectSize();
            ImGui::HelpMarker("A lower value causes light to travel further");

            ImGui::SliderFloat("Clamp", &settings.MaxValue, 1, 2);
            ImGui::HelpMarker("The maximum brightness of any surface");

            //ImGui::SliderFloat("Light plane", &settings.LightPlaneTolerance, -0.01f, -1);
            //ImGui::HelpMarker("Tolerance to use when determining if a light should hit a surface.\nReduce if undesired bleeding occurs.");

            //ImGui::SliderFloat("Attenuation B", &_settings.B, 0, 2.00);
            //ImGui::SliderFloat("Light Radius Mult", &_settings.Radius, 0, 60.0);

            //ImGui::Checkbox("Clamp", &_settings.Clamp);
            //ImGui::SameLine();
            //ImGui::SetNextItemWidth(sz.x - ImGui::GetCursorPosX() - 46); // not sure where the 46 comes from
            //ImGui::SliderFloat("Strength", &_settings.ClampStrength, 0, 1.0f);
            //ImGui::HelpMarker("Clamps the maximum brightness. At 1.0 overbright lighting is disabled.");

            //ImGui::SliderFloat("Max Distance", &_settings.DistanceThreshold, 40, 200);
            ImGui::SliderFloat("Light Radius", &settings.Radius, 10, 40);

            DrawHeader("Radiosity");
            {
                ImGui::SliderInt("Bounces", &settings.Bounces, 0, 5);
                ImGui::SliderFloat("Reflectance", &settings.Reflectance, 0, 1);
                ImGui::HelpMarker("How much light to keep after each bounce");
                //ImGui::Checkbox("Skip first bounce", &settings.SkipFirstPass);
                //ImGui::HelpMarker("Experimental: Skip the first bounce of radiosity.\nReduces artifacting and smoothes the final result but loses saturation.");
            }

            DrawHeader("Options");
            {
                ImGui::Checkbox("Occlusion", &settings.EnableOcclusion);
                ImGui::HelpMarker("Causes level geometry to block light");
                ImGui::SameLine();
                ImGui::Checkbox("Accurate Volumes", &settings.AccurateVolumes);
                ImGui::HelpMarker("Calculates light on connected sides to improve volumetric accuracy.\nHas a high performance impact.");

                ImGui::Checkbox("Color", &settings.EnableColor);
                ImGui::HelpMarker("Enables colored lighting. Currently is not saved to the level.");

                ImGui::SameLine();
                ImGui::Checkbox("Multithread", &settings.Multithread);
                ImGui::HelpMarker("Enables multithread calculations");

                /*ImGui::Checkbox("Check Coplanar", &_settings.CheckCoplanar);
                ImGui::HelpMarker("Causes co-planar light sources to have a consistent brightness");*/
            }


            if (ImGui::Button("Light Level")) {
                Commands::LightLevel(Game::Level, settings);
                Events::LevelChanged();
            }

            ImGui::Text("Time: %.3f s", (float)Metrics::LightCalculationTime / 1000000.0f);
            ImGui::Text("Ray Casts: %s", std::to_string(Metrics::RaysCast).c_str());
            ImGui::Text("Ray Hits: %s", std::to_string(Metrics::RayHits).c_str());
            ImGui::Text("Cache hits: %s", std::to_string(Metrics::CacheHits).c_str());

            ToggleLight();
#ifdef _DEBUG
            ImGui::SameLine();
            BreakLight();
#endif
        }
    };
}