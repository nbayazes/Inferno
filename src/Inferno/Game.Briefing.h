#pragma once

#include "Briefing.h"
#include "Game.Object.h"
#include "Polymodel.h"
#include "Utility.h"

namespace Inferno {
    constexpr float BRIEFING_TEXT_SPEED = 1 / 28.0f; // 28 characters per second

    namespace Game {
        extern Inferno::Level Level;
    }

    class BriefingState {
        List<Briefing::Screen> _screens;
        int _screen = 0;
        int _page = 0;
        float _elapsed = 0;
        Object _object{};
        AnimationState _animation;

    public:
        BriefingState() = default;

        BriefingState(const Briefing& briefing, int level) {
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

        float GetElapsed() const { return _elapsed; }

        bool IsValid() const { return !_screens.empty(); }

        void Forward() {
            auto prevPage = _page;
            auto prevScreen = _screen;

            if (auto screen = GetScreen()) {
                auto visibleChars = uint(_elapsed / BRIEFING_TEXT_SPEED);

                if (auto page = GetPage()) {
                    if (visibleChars < page->VisibleCharacters && !Input::ControlDown) {
                        _elapsed = 1000.0f;
                        return; // Show all text if not finished
                    }
                }

                _page++;

                if (_page >= screen->Pages.size()) {
                    _screen++;
                    _page = 0;
                }
            }

            if (prevPage != _page || prevScreen != _screen)
                OnPageChanged();
        }

        void Back() {
            auto prevPage = _page;
            auto prevScreen = _screen;
            _page--;

            if (_page < 0) {
                // Go back one screen
                if (_screen > 0) {
                    _screen--;

                    if (auto screen = GetScreen())
                        _page = (int)screen->Pages.size() - 1;
                }
            }

            if (_page < 0) _page = 0;

            if (prevPage != _page || prevScreen != _screen)
                OnPageChanged();
        }

        void Update(float dt) {
            _elapsed += dt;

            if (auto page = GetPage()) {
                if (page->Robot != -1 || page->Model != ModelID::None)
                    _object.Rotation = Matrix3x3(Matrix(_object.Rotation) * Matrix::CreateRotationY(dt));

                if (page->Robot != -1) {
                    auto& angles = _object.Render.Model.Angles;

                    // ping-pong animation
                    if (!_animation.IsPlayingAnimation()) {
                        auto animation = _animation.Animation == Animation::Rest ? Animation::Alert : Animation::Rest;
                        //auto time = animation == Animation::Fire ? 0.5f : 1.25f;
                        _animation = StartAnimation(_object, angles, animation, 1.25f, 5, 1.0f);
                    }

                    UpdateAnimation(angles, _object, _animation, dt);
                }
            }
        }

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
        void OnPageChanged() {
            _elapsed = 0;
            _animation = {};
            _animation.Animation = Animation::Alert; // robots start in the "alert" position

            if (auto page = GetPage()) {
                if (page->Robot != -1) {
                    InitObject(Game::Level, _object, ObjectType::Robot, (int8)page->Robot, true);
                }

                if (page->Model != ModelID::None) {
                    InitObject(Game::Level, _object, ObjectType::Player, 0, true);
                    _object.Render.Model.ID = page->Model;
                }

                _object.Rotation = Matrix3x3(Matrix::CreateRotationY(-3.14f / 4)); // start facing left
            }
        }
    };
}
