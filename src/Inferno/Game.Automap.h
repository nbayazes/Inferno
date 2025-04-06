#pragma once

#include "Camera.h"
#include "Types.h"
#include "Level.h"

namespace Inferno {
    enum class AutomapVisibility { Hidden, Visible, FullMap };

    struct AutomapInfo {
        List<AutomapVisibility> Segments;
        string Threat;
        string LevelNumber;
        string HostageText;
        int RobotScore = 0;
        bool FoundExit = false;
        bool FoundBlueDoor = false;
        bool FoundGoldDoor = false;
        bool FoundRedDoor = false;
        bool FoundReactor = false;
        bool FoundEnergy = false;

        //ObjID Reactor = ObjID::None;
        //Tag Exit;

        AutomapInfo() = default;
        AutomapInfo(const Inferno::Level& level);

        void Update(const Inferno::Level& level);

        // Reveal the 'full map' powerup
        void RevealFullMap() {
            for (auto& seg : Segments) {
                if (seg == AutomapVisibility::Hidden)
                    seg = AutomapVisibility::FullMap;
            }
        }

        // Reveals the entire map
        void RevealAll() {
            ranges::fill(Segments, AutomapVisibility::Visible);
        }
    };

    namespace Game {
        inline AutomapInfo Automap;
        inline Camera AutomapCamera;
    }

    void OpenAutomap();
    void CloseAutomap();
    void HandleAutomapInput();
}