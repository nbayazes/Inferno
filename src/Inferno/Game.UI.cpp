#include "pch.h"
#include "Game.Text.h"
#include "Input.h"
#include "Types.h"
#include "Utility.h"

namespace Inferno::UI {
    using Action = std::function<void()>;

    // Controls are positioned at their top left corner
    class ControlBase {
    public:
        bool Focused = false;
        bool Hovered = false;
        bool Enabled = true;
        bool Focusable = true;

        Vector2 Position;
        Vector2 Offset; // Offset from anchor point
        float Width, Height;
        AlignH HorizontalAlignment = AlignH::Left;
        AlignV VerticalAlignment = AlignV::Top;

        bool Contains(const Vector2& point) const {
            return point.x > Position.x && point.x < Position.x + Width &&
                point.y > Position.y && point.y < Position.y + Height;
        }

        virtual void OnUpdate() {
            if (Focusable && Enabled) {
                Hovered = Contains(Input::MousePosition);
            }
        }

        Vector2 GetScreenPosition;
        List<ControlBase> Children;

        virtual void OnDraw() {}

        // Called when the control is clicked via some input device
        Action OnClick = nullptr;
        virtual ~ControlBase() = default;
    };

    // Horizontal slider
    template <typename TValue>
    class Slider : public ControlBase {
    public:
        float Width;
        TValue Min, Max, Value;

        void Clicked(const Vector2& position) {
            if (!Contains(position)) return;

            // determine the location within the slider and set the value
        }
    };

    class Label : public ControlBase {
    public:
        string Text;

        Label(string_view text) : Text(text) {
            Focusable = false;
        }
    };

    class ScreenBase {
    public:
        List<ControlBase> Controls;

        Vector2 Size;

        int SelectionIndex = 0;

        void OnClick(const Vector2& position) {
            for (auto& control : Controls) {
                if (control.Enabled && control.Contains(position) && control.OnClick) {
                    control.OnClick();
                }
            }
        }

        void OnConfirm() {
            if (Seq::inRange(Controls, SelectionIndex))
                if (Controls[SelectionIndex].OnClick)
                    Controls[SelectionIndex].OnClick();
        }
    };

    List<ScreenBase> Screens;

    void ShowScreen(const ScreenBase& screen) {
        Screens.push_back(screen);
    }

    void CloseScreen() {
        if (Screens.size() == 1) return; // Can't close the last screen

        Screens.pop_back();
    }

    void DrawBackground() {}

    void Render() {
        DrawBackground();

        if (Screens.empty()) return;

        // Draw the top screen
        auto& screen = Screens.back();
        //screen.Draw();
    }

    class MainMenu : public ScreenBase {
        MainMenu() {
            Controls.push_back(Label("Start Game"));
            Controls.push_back(Label("Load Game"));
            Controls.push_back(Label("Options"));
            Controls.push_back(Label("High Scores"));
            Controls.push_back(Label("Credits"));
            Controls.push_back(Label("Quit"));
        }

        void Update() {}
    };


    void Update() {
        if (Screens.empty()) return;
        auto& screen = Screens.back();

        for (auto& control : screen.Controls) {
            control.OnUpdate();
        }
    }

    void HandleInput() {
        if (Screens.empty()) return;
        auto& screen = Screens.back();

        // todo: add controller dpad input
        if (Input::IsKeyPressed(Input::Keys::Down))
            screen.SelectionIndex++;

        if (Input::IsKeyPressed(Input::Keys::Up))
            screen.SelectionIndex--;

        // Wrap selection
        screen.SelectionIndex = Mod(screen.SelectionIndex, screen.Controls.size());

        if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick))
            screen.OnClick(Input::MousePosition);

        // todo: add controller input
        if (Input::IsKeyPressed(Input::Keys::Enter) || Input::IsKeyPressed(Input::Keys::Space))
            screen.OnConfirm();

        if (Input::IsKeyPressed(Input::Keys::Escape)) {
            CloseScreen();
            return;
        }


        //if (screen.Controls.empty()) return;
        //auto& control = screen.Controls[screen.SelectionIndex];

        //if (Input::IsKeyPressed(Input::Keys::Enter)) {
        //    control.OnClick();
        //}
    }
}
