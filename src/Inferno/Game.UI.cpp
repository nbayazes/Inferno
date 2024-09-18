#include "pch.h"
#include "Game.UI.h"
#include "Game.Text.h"
#include "Graphics/Render.h"
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

    class Button : public ControlBase {
    public:
        string Text;

        Button(string_view text) : Text(text) {
        }

        void OnDraw() override;
    };

    enum class PanelOrientation { Horizontal, Vertical };

    class StackPanel : public ControlBase {
    public:
        PanelOrientation Orientation = PanelOrientation::Vertical;
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
            StackPanel panel;
            panel.Width = 0;
            panel.Height = 0;
            panel.Position = Vector2(55, 140);
            panel.HorizontalAlignment = AlignH::Center;
            panel.VerticalAlignment = AlignV::Top;
            panel.Children = {
                Button("Play Descent 1"),
                Button("Play Descent 2"),
                Button("Load Game"),
                Button("Options"),
                Button("Level Editor"),
                Button("High Scores"),
                Button("Credits"),
                Button("Quit")
            };

            Controls = { panel };
        }

        void Update() {}
    };


    void Update() {
        //float margin = 60;
        float titleX = 175;
        float titleY = 50;
        float menuX = 55;
        float menuY = 140;
        float titleScale = 1.25f;

        {
            Render::DrawTextInfo dti;
            dti.Font = FontSize::Big;
            dti.HorizontalAlign = AlignH::Center;
            dti.VerticalAlign = AlignV::Top;
            dti.Position = Vector2(titleX, titleY);

            //float t = Frac(Clock.GetTotalTimeSeconds() / 2) * 2;
            //float anim = 0;

            //if (t < 1) {
            //    anim = 1 - std::pow(1 - t, 4);
            //}
            //else {
            //    t -= 1;
            //    anim = 1 - t * t;
            //}

            //anim += 0.6f;

            float anim = ((sin(Clock.GetTotalTimeSeconds()) + 1) * 0.5f * 0.25f) + 0.6f;
            dti.Color = Color(1, .5f, .2f) * abs(anim) * 4;
            dti.Scale = titleScale;
            Render::HudCanvas->DrawGameText("inferno", dti);
        }

        auto logoHeight = MeasureString("inferno", FontSize::Big).y * titleScale;

        {
            Render::DrawTextInfo dti;
            dti.Font = FontSize::Small;
            dti.HorizontalAlign = AlignH::Center;
            dti.VerticalAlign = AlignV::Top;
            dti.Position = Vector2(titleX, titleY + logoHeight);
            //dti.Color = Color(0.5f, 0.5f, 1);
            //dti.Color = Color(0.5f, 0.5f, 1);
            dti.Color = Color(1, 0.7f, 0.54f);

            //Render::HudCanvas->DrawGameText("descent I", dti);
            //dti.Position.y += 15;
            //Render::HudCanvas->DrawGameText("descent II", dti);
            //dti.Position.y += 15;
            //Render::HudCanvas->DrawGameText("descent 3 enhancements enabled", dti);
        }
        //{

        //    Render::DrawTextInfo dti;
        //    dti.Font = FontSize::Small;
        //    dti.HorizontalAlign = AlignH::Right;
        //    dti.VerticalAlign = AlignV::Top;
        //    dti.Position = Vector2(-margin, margin + logoHeight + 5);
        //    dti.Color = Color(0.5f, 0.5f, 1);
        //    Render::HudCanvas->DrawGameText("descent I - descent II - descent 3 enhanced", dti);
        //}

        {
            Render::DrawTextInfo dti;
            dti.Font = FontSize::MediumGold;
            dti.HorizontalAlign = AlignH::CenterRight;
            dti.VerticalAlign = AlignV::Top;
            auto height = MeasureString("new game", FontSize::Medium).y + 2;

            dti.Color = Color(1, .9f, 0.9f) * 1.7f;
            dti.Position = Vector2(menuX, menuY);
            Render::HudCanvas->DrawGameText("play descent 1", dti);

            dti.Color = Color(1, 1, 1);
            dti.Font = FontSize::Medium;

            dti.Position.y += height;
            Render::HudCanvas->DrawGameText("play descent 2", dti);

            dti.Position.y += height;
            Render::HudCanvas->DrawGameText("load game", dti);

            dti.Position.y += height;
            Render::HudCanvas->DrawGameText("options", dti);

            dti.Position.y += height;
            Render::HudCanvas->DrawGameText("level editor", dti);

            dti.Position.y += height;
            Render::HudCanvas->DrawGameText("high scores", dti);

            dti.Position.y += height;
            Render::HudCanvas->DrawGameText("credits", dti);

            dti.Position.y += height;
            Render::HudCanvas->DrawGameText("quit", dti);
        }

        {
            Render::DrawTextInfo dti;
            dti.Font = FontSize::Small;
            dti.HorizontalAlign = AlignH::Right;
            dti.VerticalAlign = AlignV::Bottom;
            dti.Position = Vector2(-5, -5);
            dti.Color = Color(0.25f, 0.25f, 0.25f);
            //dti.Scale = 0.5f;
            Render::HudCanvas->DrawGameText("inferno 0.2.0 alpha", dti);

            dti.Position.y -= 14;
            Render::HudCanvas->DrawGameText("software 1994, 1995, 1999", dti);

            dti.Position.y -= 14;
            Render::HudCanvas->DrawGameText("portions copyright(c) parallax", dti);
        }

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
