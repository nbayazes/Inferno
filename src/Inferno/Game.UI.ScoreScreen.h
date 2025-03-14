#pragma once
#include "Game.Bindings.h"
#include "Game.UI.Controls.h"

namespace Inferno::UI {
    struct ScoreInfo {
        string LevelName;
        int LevelNumber = 0;
        DifficultyLevel Difficulty{};
        int ShieldBonus = 0;
        int EnergyBonus = 0;
        int HostageBonus = 0;
        int ShipBonus = 0; // For final level
        bool FullRescue = false;
        int SkillBonus = 0;
        int TotalBonus = 0;
        int TotalScore = 0;
        int ExtraLives = 0;
        bool FinalLevel = false;

        Player::Stats stats;

        static ScoreInfo Create(uint totalHostages);
    };

    class ScoreScreen : public ScreenBase {
        static constexpr float titleOffset = 30;
        static constexpr float statsSpacing = 150;

        StackPanel* _panel = nullptr;
        bool _secretLevel;

    public:
        ScoreScreen(const ScoreInfo& info, bool secretLevel);

        void OnUpdate() override {
            if (Game::Bindings.Pressed(GameAction::FirePrimary) || Input::MouseButtonPressed(Input::MouseButtons::LeftClick))
                Game::LoadNextLevel();
        }

        bool OnMenuAction(Input::MenuActionState action) override {
            if (action == MenuAction::Confirm || action == MenuAction::Cancel) {
                Game::LoadNextLevel();
                return true;
            }

            return false;
        }

        void OnDraw() override {
            {
                // Background
                Render::CanvasBitmapInfo cbi;
                cbi.Size = ScreenSize;
                cbi.Texture = Render::Adapter->ScoreBackground.GetSRV();
                //cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Color(1.0f, 1.0f, 1.0f);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Text Background
                //auto& material = Render::Materials->Get("menu-bg");
                auto& material = Render::Materials->Black();

                Render::CanvasBitmapInfo cbi;
                cbi.Position.y = (titleOffset - 10) * GetScale();
                cbi.Size = Vector2(statsSpacing * 2 + 20, 400) * GetScale();
                cbi.Texture = material.Handle();
                cbi.Color = Color(1, 1, 1, 0.90f);
                cbi.HorizontalAlign = AlignH::Center;
                cbi.VerticalAlign = AlignV::Top;

                if (_panel) {
                    cbi.Position.y = _panel->ScreenPosition.y - 10 * GetScale();
                    //cbi.Size.x = _panel->ScreenSize.x + 20;
                    cbi.Size.x = (statsSpacing * 2 + 20) * GetScale();
                    cbi.Size.y = _panel->ScreenSize.y + 20 * GetScale();
                }

                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            ScreenBase::OnDraw();
        }
    };
}
