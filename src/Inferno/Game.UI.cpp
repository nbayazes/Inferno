#include "pch.h"
#include "Game.UI.h"
#include "Game.Text.h"
#include "Graphics/Render.h"
#include "Input.h"
#include "Types.h"
#include "Utility.h"

namespace Inferno::UI {
    using Action = std::function<void()>;
    using ClickHandler = std::function<void(const Vector2*)>;

    const auto FOCUS_COLOR = Color(1, .9f, 0.9f) * 1.7f;

    float GetScale() {
        return Render::HudCanvas->GetScale();
    }

    // Controls are positioned at their top left corner
    struct ControlBase {
        ControlBase() = default;
        ControlBase(const ControlBase&) = delete;
        ControlBase(ControlBase&&) = default;
        ControlBase& operator=(const ControlBase&) = delete;
        ControlBase& operator=(ControlBase&&) = default;
        virtual ~ControlBase() = default;

        bool Focused = false;
        //bool Hovered = false;
        bool Enabled = true;
        bool Selectable = true;

        Vector2 Position; // Relative position from parent in canvas units
        Vector2 Size; // Size of the control in canvas units

        Vector2 ScreenPosition; // Scaled and transformed position in screen pixels
        Vector2 ScreenSize; // Size of the control in screen pixels

        Vector2 Margin;
        Vector2 Padding;

        AlignH HorizontalAlignment = AlignH::Left;
        AlignV VerticalAlignment = AlignV::Top;

        float MeasureWidth() const { return Size.x + Margin.x * 2 + Padding.x * 2; }
        float MeasureHeight() const { return Size.y + Margin.y * 2 + Padding.y * 2; }

        int Layer = 0;

        virtual void OnUpdateLayout() {
            // Arrange children relative to this control
            for (auto& control : Children) {
                control->UpdateScreenPosition(*this);
                control->Layer = Layer + 1;
                control->OnUpdateLayout();
            }
        }

        void UpdateScreenPosition(const ControlBase& parent) {
            auto scale = Render::HudCanvas->GetScale();
            ScreenPosition = Position * scale + parent.ScreenPosition + Margin * scale;
            ScreenSize = Size * scale + Padding * 2 * scale;

            auto offset = Render::GetAlignment(Size * scale, HorizontalAlignment, VerticalAlignment, parent.ScreenSize);
            ScreenPosition += offset;
        }

        bool Contains(const Vector2& point) const {
            return
                point.x > ScreenPosition.x && point.x < ScreenPosition.x + ScreenSize.x &&
                point.y > ScreenPosition.y && point.y < ScreenPosition.y + ScreenSize.y;
        }

        ControlBase* HitTestCursor(const Vector2& position) {
            if (!Enabled) return nullptr;

            if (Selectable && Contains(position)) {
                return this;
            }

            for (auto& child : Children) {
                if (auto control = child->HitTestCursor(position))
                    return control;
            }

            return nullptr;
        }

        void OnClick(const Vector2& position) const {
            for (auto& control : Children) {
                if (control->Enabled && control->Contains(position) && control->ClickAction) {
                    control->ClickAction();
                    return;
                }

                control->OnClick(position);
            }
        }

        virtual void OnUpdate() {
            if (!Enabled) return;

            if (Selectable) {
                auto pos = Input::MousePosition;
                Focused = Contains(pos);
            }

            for (auto& child : Children) {
                child->OnUpdate();
            }
        }

        ControlBase* SelectFirst() const {
            for (auto& child : Children) {
                if (child->Selectable) {
                    return child.get();
                }
                else if (auto control = child->SelectFirst()) {
                    return control;
                }
            }

            return nullptr;
        }

        ControlBase* SelectLast() const {
            for (auto& child : Children | views::reverse) {
                if (child->Selectable) {
                    return child.get();
                }
                else if (auto control = child->SelectLast()) {
                    return control;
                }
            }

            return nullptr;
        }

        struct SelectionState {
            ControlBase* Selection = nullptr;
            bool SelectNext = false; // Select the next control
            bool SelectPrev = false; // Select the previous control
        };

        SelectionState SelectPrevious(ControlBase* current) const {
            int index = -1;

            for (int i = 0; i < Children.size(); i++) {
                if (Children[i].get() == current) {
                    index = i;
                    break;
                }
            }

            if (index >= 0) {
                // The current control a child of this control
                index--;

                // todo: iterate again if not focusable
                //if(!Children[*index]->Focusable) {
                //}

                if (Seq::inRange(Children, index)) {
                    // todo: what if this item has children?
                    return { Children[index].get() };
                }
                else {
                    return { .SelectPrev = true };
                }
            }
            else {
                // Check children
                for (auto& child : Children/* | views::reverse*/) {
                    // Previous iteration indicates this should be selected
                    //if (state.SelectNext) {
                    //    return { child->SelectFirst() };
                    //}

                    SelectionState state = child->SelectPrevious(current);
                    // wrap around
                    if (state.SelectPrev) {
                        return { Children.back()->SelectLast() };
                    }

                    if (state.Selection)
                        return state;
                }
            }

            return { nullptr };
        }

        SelectionState SelectNext(ControlBase* current) const {
            int index = -1;

            for (size_t i = 0; i < Children.size(); i++) {
                if (Children[i].get() == current) {
                    index = i;
                    break;
                }
            }

            if (index >= 0) {
                // The current control a child of this control
                index++;

                // todo: iterate again if not focusable
                //if(!Children[*index]->Focusable) {
                //}
                if (Seq::inRange(Children, index)) {
                    // todo: what if this item has children?
                    return { Children[index].get() };
                }
                else {
                    return { .SelectNext = true };
                }
            }
            else {
                SelectionState state;

                // Check children
                for (auto& child : Children) {
                    // Previous iteration indicates this should be selected
                    if (state.SelectNext) {
                        return { child->SelectFirst() };
                    }

                    state = child->SelectNext(current);

                    if (state.Selection)
                        return state;
                }

                // wrap around
                if (state.SelectNext) {
                    return { Children.front()->SelectFirst() };
                }
            }

            return { nullptr };
        }

        Vector2 GetScreenPosition;
        List<Ptr<ControlBase>> Children;

        template <class T, class... Args>
        void AddChild(Args&&... args) {
            auto control = make_unique<T>(std::forward<Args>(args)...);
            //control->Parent = this;
            Children.push_back(std::move(control));
        }

        void AddChild(Ptr<ControlBase> control) {
            Children.push_back(std::move(control));
        }

        virtual void OnDraw() {
            for (auto& child : Children) {
                child->OnDraw();
            }
        }

        //virtual void OnMeasure() {}

        //    //float maxWidth = 0;
        //    //float yOffset = anchor.y;

        //    // Align children based on their anchor relative to the parent

        //    for (auto& child : Children) {
        //        auto anchor = Render::GetAlignment(child->Size, child->HorizontalAlignment, child->VerticalAlignment, Render::HudCanvas->GetSize() / Render::HudCanvas->GetScale());
        //        child->Position.y = Position.y + yOffset;
        //        child->Position.x = Position.x + anchor.x;
        //        child->UpdateScreenPosition();

        //        child->OnUpdateLayout();
        //        if (child->Size.x > maxWidth)
        //            maxWidth = child->Size.x;

        //        yOffset += child->Size.y + Spacing;
        //    }

        //    // Expand children to max width to make clicking uniform
        //    for (auto& child : Children)
        //        child->Size.x = maxWidth;

        //    Size = Vector2(maxWidth, yOffset);
        //}

        // Called when the control is clicked via some input device
        Action ClickAction;
    };

    class ScreenBase : public ControlBase {
    public:
        ScreenBase() {
            Selectable = false;
        }

        int SelectionIndex = 0;

        Vector2 LastCursorPosition;

        ControlBase* Selection = nullptr;
        ControlBase* LastGoodSelection = nullptr;

        void OnUpdate() override {
            if (LastCursorPosition != Input::MousePosition) {
                LastCursorPosition = Input::MousePosition;
                SetSelection(HitTestCursor(Input::MousePosition));
            }
        }

        void OnConfirm() {
            if (Seq::inRange(Children, SelectionIndex))
                if (Children[SelectionIndex]->ClickAction)
                    Children[SelectionIndex]->ClickAction();
        }

        void OnUpdateLayout() override {
            //UpdateScreenPosition(parent);


            //auto anchor = Render::GetAlignment(Size, HorizontalAlignment, VerticalAlignment, parent.ScreenSize);

            //ScreenPosition = Position * scale - Margin * scale + anchor * scale + parent.ScreenPosition;
            //ScreenSize = Size * scale + Margin * 2 * scale;

            // Fill the whole screen if the size is zero
            auto& canvasSize = Render::HudCanvas->GetSize();
            ScreenSize = Size == Vector2::Zero ? canvasSize : Size * GetScale();
            ScreenPosition = Render::GetAlignment(ScreenSize, HorizontalAlignment, VerticalAlignment, canvasSize);
            //screen->UpdateLayout(GetFullScreen());

            ControlBase::OnUpdateLayout();
        }

        void SetSelection(ControlBase* control) {
            if (Selection) Selection->Focused = false;
            Selection = control;
            if (control) control->Focused = true;

            if (control) LastGoodSelection = Selection;
        }

        void SelectFirst() {
            for (auto& control : Children) {
                Selection = control->SelectFirst();
                if (Selection) {
                    SetSelection(Selection);
                    break;
                }
            }
        }

        void OnUpArrow() {
            auto prev = SelectPrevious(Selection).Selection;
            if (!prev) prev = LastGoodSelection;
            SetSelection(prev);
        }

        void OnDownArrow() {
            auto next = SelectNext(Selection).Selection;
            if (!next) next = LastGoodSelection;
            SetSelection(next);
        }
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
        string _text;
        FontSize _size;

    public:
        Label(string_view text, FontSize size = FontSize::Medium) : _text(text), _size(size) {
            Selectable = false;
        }

        void OnUpdateLayout() override {
            Size = MeasureString(_text, _size);
        }

        void OnDraw() override {
            Render::DrawTextInfo dti;
            dti.Font = _size;
            dti.Color = Color(1, 1, 1);
            dti.Position = ScreenPosition / GetScale() + Margin;
            Render::HudCanvas->DrawGameText(_text, dti, Layer + 1);
        }
    };

    class Button : public ControlBase {
        string _text;

    public:
        Button(string_view text) : _text(text) {
            Size = MeasureString(_text, FontSize::Medium);
            Selectable = true;
            Padding = Vector2{ 2, 2 };
        }

        Button(string_view text, Action&& action) : _text(text) {
            ClickAction = action;
            Size = MeasureString(_text, FontSize::Medium);
            Padding = Vector2{ 2, 2 };
        }

        void OnDraw() override {
            Render::DrawTextInfo dti;
            dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
            dti.Color = Focused ? FOCUS_COLOR : Color(1, 1, 1);
            dti.Position = ScreenPosition / GetScale() + Padding;
            Render::HudCanvas->DrawGameText(_text, dti, Layer);
        }
    };

    enum class PanelOrientation { Horizontal, Vertical };

    class StackPanel : public ControlBase {
    public:
        StackPanel() { Selectable = false; }

        PanelOrientation Orientation = PanelOrientation::Vertical;
        //int Spacing = 2;

        void OnUpdateLayout() override {
            auto anchor = Render::GetAlignment(Size, HorizontalAlignment, VerticalAlignment, Render::HudCanvas->GetSize() / Render::HudCanvas->GetScale());

            if (Orientation == PanelOrientation::Vertical) {
                float maxWidth = 0;
                float maxLayoutWidth = 0;
                float yOffset = anchor.y;

                for (auto& child : Children) {
                    child->Position.y = child->Margin.y + yOffset;
                    child->Position.x = child->Margin.x;
                    child->UpdateScreenPosition(*this);

                    child->OnUpdateLayout();

                    auto width = child->MeasureWidth();
                    if(maxLayoutWidth < width) maxLayoutWidth = width;
                    if (child->Size.x > maxWidth)
                        maxWidth = child->Size.x;

                    //if (child->Margin.x > maxMargin)
                    //    maxMargin = child->Margin.x;

                    yOffset += child->Size.y + child->Margin.y * 2 + child->Padding.y * 2/* + Spacing*/;
                }

                // Expand children to max width to make clicking uniform
                for (auto& child : Children)
                    child->Size.x = maxWidth;

                Size = Vector2(maxLayoutWidth/* + maxMargin * 2*/, yOffset);
            }
            else {
                float maxHeight = 0;
                float maxMargin = 0;
                float xOffset = anchor.x;

                for (auto& child : Children) {
                    child->Position.x = Position.x + anchor.x + xOffset;
                    child->Position.y = Position.y + anchor.y;
                    child->UpdateScreenPosition(*this);

                    child->OnUpdateLayout();
                    if (child->Size.y > maxHeight)
                        maxHeight = child->Size.y;

                    if (child->Margin.x > maxMargin)
                        maxMargin = child->Margin.x;

                    xOffset += child->Size.x + child->Margin.x * 2 + child->Padding.x * 2/* + Spacing*/;
                }

                // Expand children to max height to make clicking uniform
                for (auto& child : Children)
                    child->Size.y = maxHeight;

                Size = Vector2(xOffset, maxHeight);
            }
        }
    };


    List<Ptr<ScreenBase>> Screens;

    ScreenBase GetFullScreen() {
        ScreenBase fullScreen{};
        fullScreen.ScreenSize = Render::HudCanvas->GetSize() / Render::HudCanvas->GetScale();
        return fullScreen;
    }

    void ShowScreen(Ptr<ScreenBase> screen) {
        //screen->UpdateLayout(GetFullScreen());
        //main.UpdateLayout();
        //Screens.push_back(make_unique<MainMenu>(std::move(main)));
        screen->Layer = (int)Screens.size();
        screen->OnUpdateLayout();
        Screens.push_back(std::move(screen));
    }

    //void ShowScreen(ScreenBase&& screen) {
    //    screen.UpdateLayout();
    //    //main.UpdateLayout();
    //    //Screens.push_back(make_unique<MainMenu>(std::move(main)));
    //    Screens.push_back(std::move(screen));
    //}

    void CloseScreen() {
        if (Screens.size() == 1) return; // Can't close the last screen

        Screens.pop_back();
    }

    void DrawBackground() {}

    void Render() {
        DrawBackground();

        //if (Screens.empty()) return;

        // Draw the top screen
        //auto& screen = Screens.back();
        //screen.Draw();
    }

    class PlayD1Screen : public ScreenBase {
    public:
        PlayD1Screen() {
            HorizontalAlignment = AlignH::Center;
            VerticalAlignment = AlignV::Center;
            Size = Vector2(620, 460);

            // todo: scan d1/missions folder for levels

            Label title("Select mission", FontSize::Big);
            title.VerticalAlignment = AlignV::Top;
            title.HorizontalAlignment = AlignH::Center;
            title.Position = Vector2(0, 20);
            AddChild(make_unique<Label>(std::move(title)));
        }

        void OnDraw() override {
            auto scale = Render::HudCanvas->GetScale();

            Render::CanvasBitmapInfo cbi;
            cbi.Position = ScreenPosition;
            cbi.Size = ScreenSize;
            cbi.Texture = Render::Materials->White().Handle();
            cbi.Color = Color(0.1f, 0.1f, 0.1f, 1);
            Render::HudCanvas->DrawBitmap(cbi, 1);

            cbi.Color = Color(0.0f, 0.0f, 0.0f, 1);
            cbi.Size -= Vector2(40, 100) * scale;
            cbi.Position += Vector2(20, 75) * scale;
            Render::HudCanvas->DrawBitmap(cbi, 1);

            //auto title = make_unique<Label>("Play mission", FontSize::Big);
            //AddChild(std::move(title));

            //{
            //    Render::DrawTextInfo dti;
            //    dti.Font = FontSize::Big;
            //    dti.HorizontalAlign = AlignH::Center;
            //    dti.VerticalAlign = AlignV::Top;
            //    dti.Position = Vector2(0, 20);
            //    dti.Color = Color(1, 1, 1, 1);
            //    Render::HudCanvas->DrawGameText("Play Mission", dti, 2);
            //}

            {
                Render::DrawTextInfo dti;
                dti.Font = FontSize::MediumGold;
                dti.HorizontalAlign = AlignH::Left;
                dti.VerticalAlign = AlignV::Top;
                dti.Position = Vector2(50 * scale, 100);
                dti.Color = Color(1, 1, 1, 1);
                Render::HudCanvas->DrawGameText("Descent: First Strike", dti, 2);
            }

            ScreenBase::OnDraw();
        }
    };

    class MainMenu : public ScreenBase {
    public:
        MainMenu() {
            StackPanel panel;
            panel.Position = Vector2(45, 140);
            panel.HorizontalAlignment = AlignH::CenterRight;
            panel.VerticalAlignment = AlignV::Top;

            panel.AddChild<Button>("Play Descent 1", [] {
                ShowScreen(make_unique<PlayD1Screen>());
            });
            panel.AddChild<Button>("Play Descent 2");
            panel.AddChild<Button>("Load Game");
            panel.AddChild<Button>("Options");
            panel.AddChild<Button>("Level Editor");
            panel.AddChild<Button>("High Scores");
            panel.AddChild<Button>("Credits");
            panel.AddChild<Button>("Quit", [] {
                PostMessage(Shell::Hwnd, WM_CLOSE, 0, 0);
            });

            Children.push_back(make_unique<StackPanel>(std::move(panel)));
            SelectFirst();
        }

        void OnDraw() override {
            ScreenBase::OnDraw();

            float titleX = 167;
            float titleY = 50;
            float titleScale = 1.25f;
            //auto logoHeight = MeasureString("inferno", FontSize::Big).y * titleScale;

            {
                //Render::DrawTextInfo dti;
                //dti.Font = FontSize::Small;
                //dti.HorizontalAlign = AlignH::Center;
                //dti.VerticalAlign = AlignV::Top;
                //dti.Position = Vector2(titleX, titleY + logoHeight);
                ////dti.Color = Color(0.5f, 0.5f, 1);
                ////dti.Color = Color(0.5f, 0.5f, 1);
                //dti.Color = Color(1, 0.7f, 0.54f);

                //Render::HudCanvas->DrawGameText("descent remastered", dti);
                //dti.Position.y += 15;
                //Render::HudCanvas->DrawGameText("descent II", dti);
                //dti.Position.y += 15;
                //Render::HudCanvas->DrawGameText("descent 3 enhancements enabled", dti);
            }

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

                float anim = (((float)sin(Clock.GetTotalTimeSeconds()) + 1) * 0.5f * 0.25f) + 0.6f;
                dti.Color = Color(1, .5f, .2f) * abs(anim) * 4;
                dti.Scale = titleScale;
                Render::HudCanvas->DrawGameText("inferno", dti);
            }
        }
    };

    void HandleInput() {
        if (Screens.empty()) return;
        auto& screen = Screens.back();

        // todo: add controller dpad input
        if (Input::IsKeyPressed(Input::Keys::Down))
            screen->OnDownArrow();
        //screen->SelectNext(screen->Selection);
        //screen->SelectionIndex++;

        if (Input::IsKeyPressed(Input::Keys::Up))
            screen->OnUpArrow();

        // Wrap selection
        if (!screen->Children.empty())
            screen->SelectionIndex = (int)Mod(screen->SelectionIndex, screen->Children.size());

        if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick))
            screen->OnClick(Input::MousePosition);

        // todo: add controller input
        if (Input::IsKeyPressed(Input::Keys::Enter) || Input::IsKeyPressed(Input::Keys::Space))
            screen->OnConfirm();

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

    void Update() {
        //float margin = 60;

        //float menuX = 55;
        //float menuY = 140;


        //{

        //    Render::DrawTextInfo dti;
        //    dti.Font = FontSize::Small;
        //    dti.HorizontalAlign = AlignH::Right;
        //    dti.VerticalAlign = AlignV::Top;
        //    dti.Position = Vector2(-margin, margin + logoHeight + 5);
        //    dti.Color = Color(0.5f, 0.5f, 1);
        //    Render::HudCanvas->DrawGameText("descent I - descent II - descent 3 enhanced", dti);
        //}

        //{
        //    Render::DrawTextInfo dti;
        //    dti.Font = FontSize::MediumGold;
        //    dti.HorizontalAlign = AlignH::CenterRight;
        //    dti.VerticalAlign = AlignV::Top;
        //    auto height = MeasureString("new game", FontSize::Medium).y + 2;

        //    dti.Color = Color(1, .9f, 0.9f) * 1.7f;
        //    dti.Position = Vector2(menuX, menuY);
        //    Render::HudCanvas->DrawGameText("play descent 1", dti);

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
            Render::HudCanvas->DrawGameText("portions (c) parallax", dti);
        }

        if (Screens.empty()) {
            ShowScreen(make_unique<MainMenu>());
        }

        HandleInput();

        auto& screen = Screens.back();
        screen->OnUpdate();
        screen->OnUpdateLayout();
        screen->OnDraw();

        std::function<void(ControlBase&)> debugDraw = [&](const ControlBase& control) {
            for (auto& child : control.Children) {
                Render::CanvasBitmapInfo cbi;
                cbi.Position = child->ScreenPosition;
                cbi.Size = child->ScreenSize;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Color(0.1f, 1.0f, 0.1f, 0.0225f);
                Render::HudCanvas->DrawBitmap(cbi, 9);

                debugDraw(*child.get());
            }
        };

        //debugDraw(*screen.get());
    }
}
