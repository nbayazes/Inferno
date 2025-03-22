#pragma once

namespace Inferno {
    enum class GameState {
        Startup,
        Game, // In first person and running game logic
        PhotoMode, // In-game photo mode
        EscapeSequence, // exit tunnel sequence
        Cutscene, // In-game cutscene, waits for input to cancel
        LoadLevel, // Show a loading screen and load the currently pending level
        ScoreScreen,
        FailedEscape, // Player failed to escape in time
        Automap,
        Briefing, // Showing a briefing before a level
        MainMenu, // The title menu
        PauseMenu, // In-game menu
        Editor
    };

    struct LoadLevelState {
        bool startMission; // Reset player mission stats
        string fileName;
        //filesystem::path missionPath;
        bool restart = false; // resets the player state to when the level was first loaded
        string message = "prepare for descent";
    };

    struct ShowBriefingState {
        int levelNumber = 0;
        bool endgame = false; // changes the music
        string name; // name of briefing file
        bool showScore = false; // Shows score screen afterwards
    };

    struct ShowScoreScreenState {};

    //struct StateChange {
    //    GameState state;

    //    union {
    //        LoadLevelState loadLevel;
    //        ShowBriefingState briefing;
    //        // etc
    //    };

    //    ~StateChange() {
    //        switch (state) {
    //            case GameState::LoadLevel:
    //                std::destroy_at(&loadLevel);
    //                break;
    //            case GameState::Briefing:
    //                std::destroy_at(&briefing);
    //                break;
    //            //default:
    //            //    ASSERT("State destructor not implemented");
    //        }
    //    }

    //    StateChange(const StateChange&) = default;
    //    StateChange(StateChange&&) = default;
    //    StateChange& operator=(const StateChange&) = default;
    //    StateChange& operator=(StateChange&&) = default;
    //};
}
