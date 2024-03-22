#pragma once

#include "Briefing.h"
#include "Game.h"
#include "Settings.h"
#include "WindowBase.h"
#include "Resources.h"

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

        inline static Briefing DebugBriefing;
        inline static float Elapsed = 0;

    protected:
        static void NextPage() {
            if (auto screen = Seq::tryItem(DebugBriefing.Screens, Game::BriefingScreen)) {
                auto visibleChars = uint(Elapsed / Game::BRIEFING_TEXT_SPEED);

                if (auto page = Seq::tryItem(screen->Pages, Game::BriefingPage)) {
                    if (visibleChars < page->VisibleCharacters && !Input::ControlDown) {
                        Elapsed = 1000.0f;
                        return; // Show all text if not finished
                    }
                }

                Elapsed = 0;
                Game::BriefingPage++;

                if (Game::BriefingPage >= screen->Pages.size()) {
                    // Go forward one screen
                    if (Game::BriefingScreen < DebugBriefing.Screens.size()) {
                        Game::BriefingScreen++;
                        Game::BriefingPage = 0;
                    }
                }
            }
            else {
                // Something went wrong
                Game::BriefingScreen = Game::BriefingPage = 0;
                Elapsed = 0;
            }
        }

        static void PreviousPage() {
            Game::BriefingPage--;
            Elapsed = 0;

            if (Game::BriefingPage < 0) {
                // Go back one screen
                if (Game::BriefingScreen > 0) {
                    Game::BriefingScreen--;
                    if (auto screen = Seq::tryItem(DebugBriefing.Screens, Game::BriefingScreen)) {
                        Game::BriefingPage = (int)screen->Pages.size() - 1;
                    }
                }
            }

            if (Game::BriefingPage < 0) Game::BriefingPage = 0;
        }

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

            Elapsed += Render::FrameTime;

            {
                ImGui::BeginGroup();

                ImGui::BeginChild("editor", { 0, 0 }, true);

                if (ImGui::Button("Prev"))
                    PreviousPage();

                ImGui::SameLine();

                if (ImGui::Button("Next"))
                    NextPage();

                ImGui::SameLine();

                ImGui::Text("Screen: %i Page: %i", Game::BriefingScreen, Game::BriefingPage);

                if (auto screen = Seq::tryItem(DebugBriefing.Screens, Game::BriefingScreen); screen && !screen->Background.empty()) {
                    ImGui::SameLine();
                    ImGui::Text("Background: %s", screen->Background.c_str());
                }

                ImGui::InputTextMultiline("##editor", _buffer.data(), _buffer.size(), { 600, -1 }, ImGuiInputTextFlags_AllowTabInput);
                //ImGui::EndChild();

                ImGui::SameLine();

                //ImGui::BeginChild("preview", { 0, 0 }, true);
                {
                    Game::BriefingVisible = true;
                    auto srv = Render::Adapter->BriefingColorBuffer.GetSRV();
                    ImGui::Image((ImTextureID)srv.ptr, { 640, 480 });

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        NextPage();

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        PreviousPage();
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
Size:			12 meters
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
Size:			20 meters
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

        static void ResolveImages(Briefing& briefing) {
            for (auto& screen : briefing.Screens) {
                for (auto& page : screen.Pages) {
                    if (page.Image.empty()) continue;

                    if (String::Contains(page.Image, "#")) {
                        if (auto tid = Resources::LookupLevelTexID(Resources::FindTexture(page.Image)); tid != LevelTexID::None) {
                            page.Door = Resources::GetDoorClipID(tid);
                            page.Image = {}; // Clear source image
                        }
                    }
                    else if (!String::Contains(page.Image, ".")) {
                        // todo: also search for PNG, PCX, DDS
                        page.Image += ".bbm"; // Assume BBM for now
                    }
                }
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

            ResolveImages(_briefing);
            _buffer = _briefing.Raw;
            DebugBriefing = _briefing;
            Elapsed = 0;
            Game::BriefingPage = 0;
            Game::BriefingScreen = 0;
        }
    };
}
