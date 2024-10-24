#pragma once

#include "Briefing.h"
#include "Game.Briefing.h"
#include "Game.h"
#include "Resources.h"
#include "Settings.h"
#include "WindowBase.h"

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

        static void AddPyroAndReactorPages(Briefing& briefing) {
            {
                Briefing::Page pyroPage;
                pyroPage.Model = Resources::GameData.PlayerShip.Model;
                pyroPage.Text = R"($C1Pyro-GX
multi-purpose fighter
Size:			6 meters
Est. Armament:	2 Argon Lasers
				Concussion Missiles

fighter based on third generation anti-gravity tech.
excels in close quarters combat.

Effectiveness depends entirely 
on the pilot due to the lack
of electronic assistance.

veterans report that the 
pyro-gx's direct controls 
outperform newer models.)";

                // and modified for in situ upgrades

                pyroPage.VisibleCharacters = (int)pyroPage.Text.size() - 2;
                briefing.Screens[2].Pages.insert(briefing.Screens[2].Pages.begin(), pyroPage);
            }

            {
                Briefing::Page reactorPage;
                reactorPage.Model = Resources::GameData.Reactors.empty() ? ModelID::None : Resources::GameData.Reactors[0].Model;
                reactorPage.Text = R"($C1Reactor Core
PTMC fusion power source
Size:			10 meters
Est. Armament:	Pulse defense system
Threat:			Moderate

advances in fusion technology lead to the
development of small modular reactors.
these reactors have been pivotal to 
PTMC's rapid expansion and success.

upon taking significant damage 
the fusion containment field 
will fail, resulting in 
self-destruction and complete 
vaporization of the facility.
)";
                // these reactors are pivotal to PTMC's
                // financial success and rapid expansion.
                reactorPage.VisibleCharacters = (int)reactorPage.Text.size() - 2;
                briefing.Screens[2].Pages.insert(briefing.Screens[2].Pages.begin() + 1, reactorPage);
            }
                }

        void OpenBriefing(const HogEntry& entry) {
            auto data = Game::Mission->ReadEntry(entry);
            _briefing = Briefing::Read(data);

            if (Game::Level.IsDescent1()) {
                if (entry.Name == "briefing.txb") {
                    SetD1BriefingBackgrounds(_briefing, Game::Level.IsShareware);
                    AddPyroAndReactorPages(_briefing);
                }
                else if (entry.Name == "ending.txb") {
                    SetD1EndBriefingBackground(_briefing, Game::Level.IsShareware);
                }
            }

            _buffer = _briefing.Raw;
            ResolveBriefingImages(_briefing);
            Game::Briefing = BriefingState(_briefing, 0, Game::Level.IsDescent1());
        }
    };
}
