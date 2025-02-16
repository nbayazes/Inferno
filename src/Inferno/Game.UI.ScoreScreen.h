#pragma once
#include "Game.Bindings.h"
#include "Game.UI.Controls.h"

namespace Inferno::UI {
    struct ScoreInfo {
        string LevelName;
        int LevelNumber = 0;
        DifficultyLevel Difficulty{};
        string Time;
        int Secrets = 0;
        int SecretsFound = 0;
        int Deaths = 0;
        int RobotsDestroyed = 0;
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
    };

    class ScoreScreen : public ScreenBase {
        static constexpr float titleOffset = 30;
        static constexpr float statsSpacing = 150;

        StackPanel* _panel = nullptr;
        bool _secretLevel;

    public:
        ScoreScreen(const ScoreInfo& info, bool secretLevel) : _secretLevel(secretLevel) {
            constexpr float statsOffset = titleOffset + 70;
            constexpr float statsLineHeight = 20;

            {
                // title
                auto panel = AddChild<StackPanel>();
                //panel->Size.x = Size.x - DIALOG_PADDING * 2;
                //panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
                panel->HorizontalAlignment = AlignH::Center;
                panel->VerticalAlignment = AlignV::Top;
                panel->Position.y = titleOffset;
                panel->Size.x = 300;

                const auto title = info.LevelNumber > 0 ? fmt::format("Level {} complete", info.LevelNumber) : "Level complete";

                auto titleLabel = panel->AddChild<Label>(title, FontSize::MediumBlue);
                titleLabel->HorizontalAlignment = AlignH::Center;
                titleLabel->TextAlignment = AlignH::Center;
                titleLabel->Color = DIALOG_TITLE_COLOR;

                //titleLabel->Size.x = 300;
                auto levelLabel = panel->AddChild<Label>(fmt::format("{} destroyed!", info.LevelName), FontSize::MediumBlue);
                levelLabel->HorizontalAlignment = AlignH::Center;
                levelLabel->TextAlignment = AlignH::Center;
                levelLabel->Color = DIALOG_TITLE_COLOR;
            }

            {
                auto panel = AddChild<StackPanel>();
                //panel->Size.x = Size.x - DIALOG_PADDING * 2;
                //panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
                panel->HorizontalAlignment = AlignH::CenterRight;
                panel->VerticalAlignment = AlignV::Top;
                panel->Position.y = statsOffset;
                panel->Position.x = -statsSpacing;

                //Size.x = MeasureString("Graphics", FontSize::Medium).x + DIALOG_PADDING * 2;
                //Size.y += DIALOG_PADDING;

                const auto addLabel = [&](string_view label) {
                    auto child = panel->AddChild<Label>(label, FontSize::Small);
                    child->Color *= 1.2f;
                    child->Size.y = statsLineHeight;
                };

                addLabel("Difficulty");
                addLabel("Time Played");
                addLabel("Enemies Destroyed");
                addLabel("Deaths");
                if (info.Secrets > 0)
                    addLabel("Secrets");

                //panel->AddChild<Label>("Robots Destroyed");
                addLabel("");
                addLabel("Shield Bonus");
                //addLabel("Energy Bonus");

                auto hostageLabel = info.FullRescue ? "Full Rescue Bonus" : "Hostage Bonus";
                addLabel(hostageLabel);
                if (info.ShipBonus > 0)
                    addLabel("Ship Bonus");

                addLabel("Skill Bonus");
                addLabel("Total Bonus");
                addLabel("");
                addLabel("Total Score");
            }

            {
                _panel = AddChild<StackPanel>();
                //panel->Size.x = Size.x - DIALOG_PADDING * 2;
                //panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
                _panel->HorizontalAlignment = AlignH::CenterLeft;
                _panel->VerticalAlignment = AlignV::Top;
                _panel->Position.y = statsOffset;
                _panel->Position.x = statsSpacing;

                const auto addRightAligned = [&](string_view label) {
                    auto child = _panel->AddChild<Label>(label, FontSize::Small);
                    child->TextAlignment = AlignH::Right;
                    //child->Color = Color(1.0f, 0.75f, 0.6f);
                    child->Color = Color(1.0f, 0.75f, 0.4f);
                    //child->Color = Color(0.47f, 0.47f, 0.73f);
                    child->Color *= 1.35f;
                    child->Size.y = statsLineHeight;
                };

                addRightAligned(DifficultyToString(info.Difficulty));
                addRightAligned(info.Time);
                addRightAligned(std::to_string(info.RobotsDestroyed));
                addRightAligned(std::to_string(info.Deaths));
                if (info.Secrets > 0)
                    addRightAligned(fmt::format("{} of {}", info.SecretsFound, info.Secrets));

                addRightAligned("");
                addRightAligned(std::to_string(info.ShieldBonus));
                //addRightAligned(std::to_string(info.EnergyBonus));
                addRightAligned(std::to_string(info.HostageBonus));

                if (info.ShipBonus > 0)
                    addRightAligned(std::to_string(info.ShipBonus));

                addRightAligned(std::to_string(info.SkillBonus));

                //panel->AddChild<Label>("");
                addRightAligned(std::to_string(info.TotalBonus));
                addRightAligned("");
                addRightAligned(std::to_string(info.TotalScore));

                // D3 score screen:
                // Pilot performance review
                // {Level Name}
                // Mission Successful / Failed
                // Difficulty, Score, Time Played, Robots Destroyed, Shield / Energy Rating,  Number of Deaths
                // Objectives: Destroy the Reactor: Complete
            }

            if (secretLevel) {
                auto secretLabel = AddChild<Label>("Secret level found!", FontSize::Medium);
                secretLabel->VerticalAlignment = AlignV::Bottom;
                secretLabel->HorizontalAlignment = AlignH::Center;
                secretLabel->Position.y = -30;
                secretLabel->Color = INSANE_TEXT_FOCUSED;
            }

            if (info.ExtraLives > 0) {
                string label = "Extra Life!";
                if (info.ExtraLives > 1) label += fmt::format(" x{}", info.ExtraLives);

                auto extraLife = AddChild<Label>(label, FontSize::MediumGold);
                extraLife->VerticalAlignment = AlignV::Bottom;
                extraLife->HorizontalAlignment = AlignH::Center;
                extraLife->Position.y = -60;
                extraLife->Color = Color(1.75f, 1.75f, 1.75f);
            }
        }

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
