#pragma once
#include "Game.UI.ScoreScreen.h"

namespace Inferno::UI {
    void Update();
    void ShowPauseDialog();
    void ShowScoreScreen(const ScoreInfo& score, bool secretLevel);
    void ShowMainMenu();

    // missionFailed indicates the player ran out of lives
    void ShowFailedEscapeDialog(bool missionFailed);
}
