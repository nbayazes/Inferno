#pragma once

#include "Briefing.h"
#include "Game.Object.h"
#include "Polymodel.h"
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

        BriefingState(const Briefing& briefing, int level, bool isDescent1) : IsDescent1(isDescent1) {
            bool foundLevel = false;

            for (auto& screen : briefing.Screens) {
                if (screen.Level == level) {
                    _screens.push_back(screen);
                    foundLevel = true;
                }
                else if (foundLevel) {
                    break; // stop after level number changes to skip test screens
                }
            }
        }

        bool IsDescent1 = false; // Enables coordinate scaling for D1

        float GetElapsed() const { return _elapsed; }

        bool IsValid() const { return !_screens.empty(); }

        void Forward();

        void Back();

        void Update(float dt);

        void LoadBackgrounds();

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

    void HandleBriefingInput();
}
