#pragma once

#include "DirectX.h"
#include "Level.h"

namespace Inferno::Render {
    void DrawEditor(ID3D12GraphicsCommandList* cmdList, Level& level);
    void CreateEditorResources();
    void ReleaseEditorResources();
    void DrawObjectOutline(const Object&);

    // Editor colors
    namespace Colors {
        constexpr Color Wall = { 0.8f, 0.8f, 0.8f };
        constexpr Color Trigger = { 0.0f, 1.0f, 1.0f };
        constexpr Color TriggerArrow = { 1.0f, 1.0f, 0.8f, 0.9f };
        constexpr Color ReactorTriggerArrow = { 1.0f, 0.1f, 0.1f, 0.9f }; 

        constexpr Color SelectionOutline = { 1, 1, 1, 1 };
        constexpr Color SelectionPrimary = { 1, 0.2f, 0, 1 };
        constexpr Color SelectionSecondary = { 1, 0.75f, 0, 1 };
        constexpr Color SelectionTertiary = { 0, 1.0f, 0, 1 };

        constexpr float FillAlpha = 0.10f;

        constexpr Color MarkedOpenFace = { 1.0f, 0.2f, 0.2f, FillAlpha };
        constexpr Color MarkedFace = { 1.0f, 0.2f, 0.0f };
        constexpr Color MarkedFaceFill = { 1.0f, 0.2f, 0.0f, FillAlpha * 2 };
        constexpr Color MarkedWallFill = { 0.2f, 1.0f, 0.0f, FillAlpha };
        constexpr Color MarkedWall = { 0.2f, 1.0f, 0.0f };

        constexpr Color Portal = { 0.75f, 0.0f, 1.0f, 0.25f };

        constexpr Color Wireframe = { 0.75f, 0.75f, 0.75f, 0.6f };

        constexpr Color MarkedSegment = MarkedFace;
        constexpr Color MarkedSegmentFill = MarkedFaceFill;
        //constexpr Color MarkedSegment = { 0.5, 0.5, 0.5, 1 };
        //constexpr Color MarkedSegmentFill = { 0.5, 0.5, 0.5, FillAlpha / 2 };

        constexpr Color SelectedObject = { 0.1f, 0.5f, 1.0f };
        //constexpr Color MarkedObject = { 1.0f, 0.8f, 0.6f };
        constexpr Color MarkedObject = MarkedFace;
        constexpr Color MarkedPoint = MarkedFace;

        constexpr Color GlobalOrientation = { 0.4f, 0.4f, 0.4f };

        // Automap colors
        constexpr Color Door = { 0.1612903f, 0.8709677f, 0.1612903f };
        constexpr Color DoorBlue = { 0.0f, 0.0f, 1.0f };
        constexpr Color DoorGold = { 1.0f, 1.0f, 0.0f };
        constexpr Color DoorRed = { 1.0f, 0.0f, 0.0f };
        constexpr Color Revealed = { 0, 0, 0.8064516f }; // Full map revealed
        constexpr Color AutomapWall = { 0.9354839f, 0.9354839f, 0.9354839f };
        constexpr Color Hostage = { 0, 1.0f, 0 };
        constexpr Color Font = { 0.6451613f, 0.6451613f, 0.6451613f };
        constexpr Color Fuelcen = { 0.9354839f, 0.8709677f, 0.43f };
        constexpr Color Reactor = { 0.9354839f, 0, 0 };
        constexpr Color Matcen = { 0.9354839f, 0, 1 };

        constexpr Color Robot = ColorFromRGB(255, 0, 255);
        constexpr Color Player = ColorFromRGB(137, 160, 210);
        constexpr Color Powerup = ColorFromRGB(255, 177, 106);

        constexpr Color FuelcenFill = { 0.9354839f, 0.8709677f, 0.43f, FillAlpha };
        constexpr Color ReactorFill = { 0.9354839f, 0, 0, FillAlpha };
        constexpr Color MatcenFill = { 0.9354839f, 0, 1, FillAlpha };

        constexpr Color GoalRed = { 0.9354839f, 0.5, 0.5 };
        constexpr Color GoalRedFill = { 0.9354839f, 0.5, 0.5, FillAlpha * 3 };
        constexpr Color GoalBlue = { 0.5, 0.5, 0.9354839f };
        constexpr Color GoalBlueFill = { 0.5, 0.5, 0.9354839f, FillAlpha * 3 };

        constexpr Tuple<Color, Color> ForSegment(SegmentType type) {
            switch (type) {
                case SegmentType::Energy: return { Colors::Fuelcen, Colors::FuelcenFill };
                case SegmentType::Reactor: return { Colors::Reactor, Colors::ReactorFill };
                case SegmentType::Matcen: return { Colors::Matcen, Colors::MatcenFill };
                case SegmentType::GoalBlue: return { Colors::GoalBlue, Colors::GoalBlueFill };
                case SegmentType::GoalRed: return { Colors::GoalRed, Colors::GoalRedFill };
                default: return { Colors::MarkedSegment, Colors::MarkedSegmentFill };
            }
        }

    }


}