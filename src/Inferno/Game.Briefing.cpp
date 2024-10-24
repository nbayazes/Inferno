#include "pch.h"
#include "Game.Briefing.h"
#include "Game.h"
#include "Input.h"
#include "Resources.h"

namespace Inferno {
    void BriefingState::Forward() {
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

    void BriefingState::Back() {
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

    void BriefingState::Update(float dt) {
        _elapsed += dt;

        if (auto page = GetPage()) {
            if (page->Robot != -1 || page->Model != ModelID::None)
                //_object.Rotation = Matrix3x3(Matrix::CreateRotationY(dt));
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

    void BriefingState::OnPageChanged() {
        _elapsed = 0;
        _animation = {};
        _animation.Animation = Animation::Alert; // robots start in the "alert" position

        if (auto page = GetPage()) {
            if (page->Robot != -1) {
                InitObject(_object, ObjectType::Robot, (int8)page->Robot, true);
            }

            if (page->Model != ModelID::None) {
                InitObject(_object, ObjectType::Player, 0, true);
                _object.Render.Model.ID = page->Model;
            }

            _object.Rotation = Matrix3x3(Matrix::CreateRotationY(-3.14f / 4)); // start facing left
        }
    }

    void ResolveBriefingImages(Briefing& briefing) {
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

    void HandleBriefingInput() {
        using Input::Keys;

        if (Input::IsMouseButtonPressed(Input::MouseButtons::RightClick) ||
            Input::IsKeyPressed(Keys::Left))
            Game::Briefing.Back();

        bool exitBriefing = false;

        if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick) ||
            Input::IsKeyPressed(Keys::Space) ||
            Input::IsKeyPressed(Keys::Right)) {
            Game::Briefing.Forward();
            if (!Game::Briefing.GetScreen())
                exitBriefing = true;
        }

        if (Input::IsKeyPressed(Keys::Escape)) {
            // todo: show loading screen and load level
            exitBriefing = true;
        }

        if (exitBriefing) {
            Game::BriefingVisible = false;
            Game::SetState(GameState::LoadLevel);
        }
    }
}
