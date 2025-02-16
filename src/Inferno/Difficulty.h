#pragma once

namespace Inferno {
    struct LevelHit;

    enum class DifficultyLevel {
        Trainee,
        Rookie,
        Hotshot,
        Ace,
        Insane,
        Count
    };

    inline string_view DifficultyToString(DifficultyLevel difficulty) {
        switch (difficulty) {
            default:
            case DifficultyLevel::Trainee: return "Trainee";
            case DifficultyLevel::Rookie: return "Rookie";
            case DifficultyLevel::Hotshot: return "Hotshot";
            case DifficultyLevel::Ace: return "Ace";
            case DifficultyLevel::Insane: return "Insane";
        }
    }
}