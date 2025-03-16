#pragma once
#include "Camera.h"
#include "Game.Navigation.h"
#include "Level.h"

namespace Inferno {
    constexpr float ACTIVE_ROOM_DEPTH = 1000; // Portal depth for active rooms

    // Windows represent a 2D rectangle.
    // Comparisons are done with top being positive.
    struct Window {
        float Left = 0, Right = 0, Top = 0, Bottom = 0;

        // Clips the window by another window. Returns true if intersects.
        bool Clip(const Window& window) {
            if (!Intersects(window)) return false;
            Left = std::max(window.Left, Left);
            Top = std::min(window.Top, Top);
            Right = std::min(window.Right, Right);
            Bottom = std::max(window.Bottom, Bottom);
            return true;
        }

        // Returns true if the window intersects another window.
        bool Intersects(const Window& window) const {
            if (Left > window.Right || Top < window.Bottom ||
                Right < window.Left || Bottom > window.Top)
                return false;

            return true;
        }

        // Expands the window to another window. Returns true if changed.
        bool Expand(const Window& window) {
            if (window.Left < Left || window.Right > Right ||
                window.Top > Top || window.Bottom < Bottom) {
                Left = std::min(window.Left, Left);
                Top = std::max(window.Top, Top);
                Right = std::max(window.Right, Right);
                Bottom = std::min(window.Bottom, Bottom);
                return true;
            }

            return false;
        }

        // Expands the window to contain a point
        void Expand(const Vector2& point) {
            Left = std::min(point.x, Left);
            Top = std::max(point.y, Top);
            Right = std::max(point.x, Right);
            Bottom = std::min(point.y, Bottom);
        }

        bool IsEmpty() const { return Left == Right && Top == Bottom; }
    };

    constexpr auto EMPTY_WINDOW = Window{ -1, -1, -1, -1 };

    List<RoomID> GetRoomsByDepth(span<Room> rooms, RoomID startRoom, float maxDistance, TraversalFlag flags);
}
