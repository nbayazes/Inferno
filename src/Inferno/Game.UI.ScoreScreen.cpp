#include "pch.h"
#include "Game.UI.ScoreScreen.h"

namespace Inferno::UI {
    UI::ScoreInfo ScoreInfo::Create(uint totalHostages) {
        //if (level.Version != 0) {
        //    return {
        //        .LevelName = "PLACEHOLDER LEVEL",
        //        .LevelNumber = 1,
        //        .Difficulty = Game::Difficulty,
        //        .Time = "0:00",
        //        .Secrets = 3,
        //        .SecretsFound = 1,
        //        .RobotsDestroyed = 10,
        //        .ShieldBonus = 1000,
        //        .EnergyBonus = 1000,
        //        .HostageBonus = 1000,
        //        .FullRescue = true,
        //        .SkillBonus = 0,
        //        .TotalBonus = 3000,
        //        .TotalScore = 53000,
        //        .ExtraLives = 1
        //    };
        //}

        // D2 fix for secret levels having negative numbers
        //if (levelNumber < 0)
        //    levelNumber *= -(levelCount / secretLevelCount);

        auto finalLevel = Game::IsFinalLevel();
        auto& player = Game::Player;
        auto difficulty = (int)Game::Difficulty;
        auto& stats = player.stats;
        auto levelPoints = stats.score - stats.levelStartScore;

        UI::ScoreInfo score{};
        score.FinalLevel = finalLevel;
        score.stats = player.stats;

        if (!Game::Cheater) {
            if (difficulty > 1) {
                if (Game::Level.IsDescent1())
                    score.SkillBonus = levelPoints * (difficulty - 1) / 2; // D1 (0.5 to 1.5x)
                else
                    score.SkillBonus = levelPoints * difficulty / 4; // D2 (0.5 to 1x)

                score.SkillBonus -= score.SkillBonus % 100; // round
            }

            // D2 uses level number for shield and energy bonus, D1 uses difficulty level
            //score.ShieldBonus = (int)player.Shields * 5 * levelNumber;
            //score.EnergyBonus = (int)player.Energy * 2 * levelNumber;
            score.ShieldBonus = (int)player.Shields * 10 * (difficulty + 1);

            // Remove energy bonus, it's kind of lame and rewards guass / backtracking to an energy center
            //score.EnergyBonus = (int)player.Energy * 5 * (difficulty + 1);
            //score.EnergyBonus = std::max((int)player.Energy - 100, 0) * 10 * (difficulty + 1);
            score.HostageBonus = stats.hostagesOnboard * 500 * (difficulty + 1);

            score.ShieldBonus -= score.ShieldBonus % 50;
            score.EnergyBonus -= score.EnergyBonus % 50;

            if (stats.hostagesOnboard == totalHostages) {
                score.HostageBonus += stats.hostagesOnboard * 1000 * (difficulty + 1);

                score.FullRescue = true;
            }

            // Convert extra lives to points on the final level
            if (finalLevel) {
                score.ShipBonus = player.Lives * 10000;

                // add current level stats, as the usual start level method clears the current level
                score.stats.totalTime += stats.time;
                score.stats.totalKills += stats.kills;
                score.stats.totalDeaths += stats.deaths;
                score.stats.totalRobots += stats.robots;
            }
        }

        score.Difficulty = Game::Difficulty;
        score.TotalBonus = score.SkillBonus + score.EnergyBonus + score.ShieldBonus + score.HostageBonus + score.ShipBonus;
        score.ExtraLives = Game::AddPointsToScore(score.TotalBonus);

        // don't show extra lives on the final level (they were just removed for bonus points)
        if (finalLevel) score.ExtraLives = 0;
        score.TotalScore = player.stats.score;
        score.LevelNumber = Game::LevelNumber;
        score.LevelName = Game::Level.Name;
        return score;
    }

    ScoreScreen::ScoreScreen(const ScoreInfo& info, bool secretLevel): _secretLevel(secretLevel) {
        constexpr float statsOffset = titleOffset + 70;
        constexpr float statsLineHeight = 20;

        auto& stats = info.stats;

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

            if (info.stats.secrets > 0)
                addLabel("Secrets");

            if (info.FinalLevel) {
                addLabel("");
                addLabel("Total Time Played");
                addLabel("Total Enemies Destroyed");
                addLabel("Total Deaths");
            }

            //panel->AddChild<Label>("Robots Destroyed");
            addLabel("");
            addLabel("Shield Bonus");
            //addLabel("Energy Bonus");

            auto hostageLabel = info.FullRescue ? "Full Rescue Bonus" : "Hostage Bonus";
            addLabel(hostageLabel);
            if (info.FinalLevel)
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
            addRightAligned(FormatDisplayTime(stats.time));

            addRightAligned(std::to_string(stats.kills));
            addRightAligned(std::to_string(stats.deaths));
            if (stats.secrets > 0)
                addRightAligned(fmt::format("{} of {}", stats.secretsFound, stats.secrets));

            if (info.FinalLevel) {
                addRightAligned("");
                addRightAligned(FormatDisplayTime(stats.totalTime));
                addRightAligned(std::to_string(stats.totalKills));
                addRightAligned(std::to_string(stats.totalDeaths));
            }

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
}
