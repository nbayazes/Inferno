#include "pch.h"
#include "Game.Briefing.h"
#include "Game.Bindings.h"
#include "Game.h"
#include "Graphics.h"
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
                Graphics::LoadModel(page->Model);
                _object.Render.Model.ID = page->Model;
            }

            {
                auto& doorClip = Resources::GetDoorClip(page->Door);
                auto wids = Seq::map(doorClip.GetFrames(), Resources::LookupTexID);
                Graphics::LoadTextures(wids);
            }

            _object.Rotation = Matrix3x3(Matrix::CreateRotationY(-3.14f / 4)); // start facing left
        }
    }


    void LoadBriefingResources(BriefingState& briefing, LoadFlag loadFlags) {
        List<ModelID> models;
        List<TexID> ids;
        Set<string> files; // bitmaps used in the briefing

        // Precache resources so switching pages doesn't cause hitches
        for (auto& screen : briefing.GetScreens()) {
            filesystem::path background = screen.Background;
            auto ext = background.extension();
            background.replace_filename(background.stem().string() + "h");
            background.replace_extension(ext);

            if (Game::Mission && !Game::Mission->IsRetailMission() && Game::Mission->TryFindEntry(screen.Background)) {
                // search user mission HOG before checking high res in case it provides custom backgrounds
                files.insert(screen.Background);
            }
            else if (Resources::Find(background.string(), LoadFlag::Mission | LoadFlag::Dxa | LoadFlag::BaseHog | loadFlags)) {
                files.insert(background.string()); // Check for high res image
                screen.Background = background.string();
            }
            else {
                files.insert(screen.Background);
            }

            for (auto& page : screen.Pages) {
                if (page.Model != ModelID::None)
                    models.push_back(page.Model);

                if (page.Robot != -1) {
                    auto& info = Resources::GetRobotInfo(page.Robot);
                    models.push_back(info.Model);
                }

                if (!page.Image.empty()) {
                    if (String::Contains(page.Image, "#")) {
                        if (auto tid = Resources::LookupLevelTexID(Resources::FindTexture(page.Image)); tid != LevelTexID::None) {
                            page.Door = Resources::GetDoorClipID(tid);
                            page.Image = {}; // Clear source image
                        }
                    }
                    else if (!String::HasExtension(page.Image)) {
                        // todo: also search for PNG, PCX, DDS
                        page.Image += ".bbm"; // Assume BBM for now
                        files.insert(page.Image);
                    }
                }

                auto& doorClip = Resources::GetDoorClip(page.Door);
                auto wids = Seq::map(doorClip.GetFrames(), Resources::LookupTexID);
                Seq::append(ids, wids);
            }
        }

        Graphics::LoadTextures(ids);

        for (auto& model : models) {
            Graphics::LoadModel(model);
        }

        // Search mission HOG, then DXA (high res), then base hog
        auto f = Seq::ofSet(files);
        Graphics::LoadTextures(f, LoadFlag::Mission | loadFlags);
        Graphics::LoadTextures(f, LoadFlag::Dxa | loadFlags);
        Graphics::LoadTextures(f, LoadFlag::BaseHog | loadFlags);
    }

    void AddPyroAndReactorPages(Briefing& briefing) {
        auto screen = Seq::tryItem(briefing.Screens, 2);
        if (!screen) return;

        {
            Briefing::Page pyroPage;
            pyroPage.Model = Resources::GameData.PlayerShip.Model;
            pyroPage.Text = R"($C1Pyro-GX
multi-purpose fighter
Size:			6 meters
Est. Armament:	2 Argon Lasers
				Concussion Missiles

fighter based on third generation anti-gravity tech.
excels in close quarters combat and modified to 
equip upgrades encountered in the field.

Effectiveness depends entirely 
on the pilot due to the lack
of electronic assists.
)";
            /*
             *elite pilots report that the
             *pyro-gx's direct controls
             *outperform newer models.
             */

            pyroPage.VisibleCharacters = (int)pyroPage.Text.size() - 2;
            screen->Pages.insert(screen->Pages.begin(), pyroPage);
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

significant damage will cause
the fusion containment field
to fail, resulting in
self-destruction and complete 
vaporization of the facility.
)";
            // these reactors are pivotal to PTMC's
            // financial success and rapid expansion.
            reactorPage.VisibleCharacters = (int)reactorPage.Text.size() - 2;
            screen->Pages.insert(screen->Pages.begin() + 1, reactorPage);
        }
    }


    void HandleBriefingInput() {
        using Input::Keys;

        if (Input::MouseButtonPressed(Input::MouseButtons::RightClick) ||
            Input::OnKeyPressed(Keys::Left) ||
            Input::MenuActions.IsSet(MenuAction::Left))
            Game::Briefing.Back();

        bool exitBriefing = false;

        if (Input::MouseButtonPressed(Input::MouseButtons::LeftClick) ||
            Input::OnKeyPressed(Keys::Space) ||
            Input::OnKeyPressed(Keys::Right) ||
            Input::MenuActions.IsSet(MenuAction::Confirm) ||
            Input::MenuActions.IsSet(MenuAction::Right)) {
            Game::Briefing.Forward();
            if (!Game::Briefing.GetScreen())
                exitBriefing = true;
        }

        if (Game::Bindings.Pressed(GameAction::Pause)) {
            exitBriefing = true;
        }

        if (exitBriefing) {
            Game::BriefingVisible = false;

            // the last level shows score screen after briefing
            auto state = Game::IsFinalLevel() ? GameState::ScoreScreen : GameState::LoadLevel;
            Game::SetState(state);
        }
    }


    void ShowBriefing(const MissionInfo& mission, int levelNumber, const Inferno::Level& level, string briefingName, bool endgame) {
        if (!Game::Mission)
            return;

        if (!String::HasExtension(briefingName))
            briefingName += ".txb";

        auto entry = Game::Mission->TryReadEntry(briefingName);
        if (!entry) return;

        auto briefing = Briefing::Read(*entry, level.IsDescent1());

        auto isShareware = Game::Mission->IsShareware();

        if (level.IsDescent1()) {
            if (endgame) {
                SetD1EndBriefingBackground(briefing, isShareware);
            }
            else {
                SetD1BriefingBackgrounds(briefing, isShareware);
            }

            if (mission.Name == Game::FIRST_STRIKE_NAME && levelNumber == 1) {
                AddPyroAndReactorPages(briefing);
            }

            auto music = Game::IsFinalLevel() ? "d1/endgame" : "d1/briefing";
            Game::PlayMusic(music);
        }

        Game::Briefing = BriefingState(briefing, levelNumber, level.IsDescent1(), endgame);
        LoadBriefingResources(Game::Briefing, GetLevelLoadFlag(level));
        Game::SetState(GameState::Briefing);
    }
}
