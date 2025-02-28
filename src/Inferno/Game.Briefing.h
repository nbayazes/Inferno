#pragma once

#include "Briefing.h"
#include "Game.Object.h"
#include "Mission.h"
#include "Polymodel.h"
#include "Resources.Common.h"
#include "Utility.h"

namespace Inferno {
    constexpr float BRIEFING_TEXT_SPEED = 1 / 28.0f; // 28 characters per second

    class BriefingState {
        List<Briefing::Screen> _screens;
        int _screen = 0;
        int _page = 0;
        float _elapsed = 0;
        Object _object{};
        AnimationState _animation;

    public:
        BriefingState() = default;

        BriefingState(const Briefing& briefing, int level, bool isDescent1, bool endgame) : IsDescent1(isDescent1) {
            bool foundLevel = false;

            for (auto& screen : briefing.Screens) {
                if (isDescent1 && level == 1 && (screen.Level == 0 || screen.Level == 1)) {
                    // special case for D1 intro briefing show both level 0 and level 1
                    _screens.push_back(screen);
                    foundLevel = true;
                }
                else if (screen.Level == level || endgame) {
                    _screens.push_back(screen);
                    foundLevel = true;
                }
                else if (foundLevel) {
                    break; // stop after level number changes to skip test screens
                }
            }

            OnPageChanged(); // init animations
        }

        bool IsDescent1 = false; // Enables coordinate scaling for D1

        span<Briefing::Screen> GetScreens() { return _screens; }

        float GetElapsed() const { return _elapsed; }

        bool IsValid() const { return !_screens.empty(); }

        void Forward();

        void Back();

        void Update(float dt);

        // Returns the current screen, or null if past the end
        const Briefing::Screen* GetScreen() const {
            return Seq::tryItem(_screens, _screen);
        }

        // Returns the current page, or null if past the end
        const Briefing::Page* GetPage() const {
            if (auto screen = GetScreen())
                return Seq::tryItem(screen->Pages, _page);

            return nullptr;
        }

        const Object* GetObject() const {
            if (auto page = GetPage()) {
                if (page->Robot != -1 || page->Model != ModelID::None)
                    return &_object;
            }

            return nullptr;
        }

    private:
        void OnPageChanged();
    };

    // Converts image names into resources
    void LoadBriefingResources(BriefingState& briefing, LoadFlag loadFlags);
    void HandleBriefingInput();

    // Adds pyro and reactor description pages to the D1 briefing
    void AddPyroAndReactorPages(Briefing& briefing);

    // Changes the game state to show a briefing
    void ShowBriefing(const MissionInfo& mission, int levelNumber, const Inferno::Level& level, string briefingName, bool endgame);
}
