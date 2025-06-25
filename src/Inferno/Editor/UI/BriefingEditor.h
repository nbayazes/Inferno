#pragma once

#include "Briefing.h"
#include "Game.Briefing.h"
#include "Game.h"
#include "Settings.h"
#include "WindowBase.h"
#include "Hog.IO.h"

namespace Inferno::Editor {
    class BriefingEditor : public WindowBase {
        int _txbIndex = 0;
        string _buffer;
        Briefing _briefing;
        static constexpr int BUFFER_SIZE = 2048 * 10;

    public:
        BriefingEditor() : WindowBase("Briefing Editor", &Settings::Editor.Windows.BriefingEditor) {
            _buffer.resize(BUFFER_SIZE);
        }

    protected:
        void OnUpdate() override {
            if (!Game::Mission) {
                ImGui::Text("Current file is not a mission (HOG)");
                return;
            }

            ImGui::BeginChild("pages", { 200, 0 }, true);
            for (auto& entry : Game::Mission->Entries) {
                if (!entry.IsBriefing() || !entry.Index) continue;
                if (ImGui::Selectable(entry.Name.c_str(), _txbIndex == *entry.Index, ImGuiSelectableFlags_AllowDoubleClick)) {
                    _txbIndex = *entry.Index;
                    if (ImGui::IsMouseDoubleClicked(0))
                        OpenBriefing(entry);
                }
            }

            ImGui::EndChild();

            ImGui::SameLine();

            Game::Briefing.Update(Inferno::Clock.GetFrameTimeSeconds());
            //Game::Briefing.Update(Render::FrameTime);

            {
                ImGui::BeginGroup();

                ImGui::BeginChild("editor", { 0, 0 }, true);

                if (ImGui::Button("Back"))
                    Game::Briefing.Back();

                ImGui::SameLine();

                if (ImGui::Button("Next"))
                    Game::Briefing.Forward();

                //ImGui::SameLine();

                //ImGui::Text("Screen: %i Page: %i", Game::BriefingScreen, Game::BriefingPage);

                /*if (auto screen = Seq::tryItem( DebugBriefing.Screens, Game::BriefingScreen); screen && !screen->Background.empty()) {
                    ImGui::SameLine();
                    ImGui::Text("Background: %s", screen->Background.c_str());
                }*/

                ImGui::InputTextMultiline("##editor", _buffer.data(), _buffer.size(), { 600, -1 }, ImGuiInputTextFlags_AllowTabInput);
                //ImGui::EndChild();

                ImGui::SameLine();

                //ImGui::BeginChild("preview", { 0, 0 }, true);
                {
                    Game::BriefingVisible = true;
                    auto srv = Render::Adapter->BriefingColorBuffer.GetSRV();
                    ImGui::Image((ImTextureID)srv.ptr, { 640, 480 });

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        Game::Briefing.Forward();

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        Game::Briefing.Back();
                }

                //ImGui::SameLine();
                //{
                //    auto srv = Render::Adapter->BriefingScanlineBuffer.GetSRV();
                //    ImGui::Image((ImTextureID)srv.ptr, { 640, 480 });
                //}

                ImGui::EndChild();


                ImGui::EndGroup();
            }


            /*ImGui::DragFloat3("Strength", &_strength.x, 1, 0, 100, "%.2f");
            ImGui::HelpMarker("Maximum amount of movement on each axis");

            ImGui::DragFloat("Scale", &_scale, 1.0f, 1, 1000, "%.2f");
            ImGui::HelpMarker("A higher scale creates larger waves\nwith less localized noise");

            ImGui::DragInt("Seed", &_seed);
            ImGui::Checkbox("Random Seed", &_randomSeed);

            if (ImGui::Button("Apply", { 100, 0 })) {
                Commands::ApplyNoise(_scale, _strength, _seed);
                if (_randomSeed) _seed = rand();
            }*/
        }

        void OpenBriefing(const HogEntry& entry) {
            {
                HogReader mission(Game::Mission->Path);
                auto data = mission.ReadEntry(entry.Name);
                _briefing = Briefing::Read(data, Game::Level.IsDescent1());
            }

            string briefing = "briefing.txb";
            string ending = "ending.txb";

            if (auto missionInfo = Game::GetCurrentMissionInfo()) {
                briefing = missionInfo->GetValue("briefing");
                ending = missionInfo->GetValue("ending");
                if (!String::HasExtension(briefing)) briefing += ".txb";
                if (!String::HasExtension(ending)) ending += ".txb";
            }

            bool endgame = false;

            if (Game::Level.IsDescent1()) {
                if (entry.Name == briefing) {
                    SetD1BriefingBackgrounds(_briefing, Game::Level.IsShareware);
                }
                else if (entry.Name == ending) {
                    SetD1EndBriefingBackground(_briefing, Game::Level.IsShareware);
                    endgame = true;
                }
            }

            _buffer = _briefing.Raw;
            Game::Briefing = BriefingState(_briefing, 0, Game::Level.IsDescent1(), endgame);
            LoadBriefingResources(Game::Briefing, GetLevelLoadFlag(Game::Level));
        }
    };
}
