#pragma once
#include "Types.h"
#include "Game.Text.h"
#include "Graphics/Render.h"
#include "gsl/pointers.h"
#include "SoundSystem.h"
#include "Input.h"

namespace Inferno::UI {
    constexpr auto MENU_SELECT_SOUND = "data/menu-select3.wav";
    constexpr auto MENU_BACK_SOUND = "data/menu-back1.wav";
    constexpr Color HOVER_COLOR = { 1, .9f, 0.9f };
    const auto FOCUS_COLOR = HOVER_COLOR * 1.7f;
    constexpr Color ACCENT_COLOR = { 1, .75f, .2f };
    const auto ACCENT_GLOW = ACCENT_COLOR * 2.0f;
    constexpr Color BORDER_COLOR = { 0.25f, 0.25f, 0.25f };
    constexpr Color IDLE_BUTTON = { 0.4f, 0.4f, 0.4f };
    constexpr Color DESELECT_IDLE_BUTTON = { 0.25f, 0.25f, 0.25f };
    constexpr Color DIALOG_TITLE_COLOR = { 1.25f, 1.25f, 2.0f };
    constexpr Color DIALOG_BACKGROUND = { 0.1f, 0.1f, 0.1f };
    constexpr Color HELP_TEXT_COLOR = { 0.75f, 0.75f, 0.75f };
    constexpr float DIALOG_PADDING = 15;
    constexpr float DIALOG_CONTENT_PADDING = DIALOG_PADDING + 30;

    using Action = std::function<void()>;

    inline float GetScale() {
        return Render::UICanvas->GetScale();
    }

    // Returns true if a rectangle at a position and size contain a point
    inline bool RectangleContains(const Vector2 origin, const Vector2& size, const Vector2& point) {
        return
            point.x > origin.x && point.x < origin.x + size.x &&
            point.y > origin.y && point.y < origin.y + size.y;
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
        bool Hovered = false;
        bool Enabled = true;
        bool Selectable = true;

        bool DockFill = true; // Match size of parent layout container

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

        int Layer = -1;

        virtual void OnUpdateLayout() {
            // Arrange children relative to this control
            for (auto& control : Children) {
                control->UpdateScreenPosition(*this);
                control->Layer = Layer + 1;
                control->OnUpdateLayout();
            }
        }

        void UpdateScreenPosition(const ControlBase& parent) {
            auto scale = Render::UICanvas->GetScale();
            ScreenPosition = Position * scale + parent.ScreenPosition + Margin * scale;
            ScreenSize = Size * scale + Padding * 2 * scale;

            auto offset = Render::GetAlignment(Size * scale, HorizontalAlignment, VerticalAlignment, parent.ScreenSize, Margin * scale);
            ScreenPosition += offset;
        }

        bool Contains(const Vector2& point) const {
            return
                point.x > ScreenPosition.x && point.x < ScreenPosition.x + ScreenSize.x &&
                point.y > ScreenPosition.y && point.y < ScreenPosition.y + ScreenSize.y;
        }

        virtual ControlBase* HitTestCursor() {
            if (!Enabled) return nullptr;

            if (Selectable && Contains(Input::MousePosition)) {
                return this;
            }

            for (auto& child : Children) {
                if (auto control = child->HitTestCursor())
                    return control;
            }

            return nullptr;
        }

        void OnClick(const Vector2& position) const {
            for (auto& control : Children) {
                if (control->Enabled && control->Contains(position) && control->ClickAction) {
                    Sound::Play2D(SoundResource{ control->ActionSound });
                    control->ClickAction();
                    return;
                }

                control->OnClick(position);
            }
        }

        virtual void OnUpdate() {
            if (!Enabled) return;

            if (Input::MouseMoved()) {
                Hovered = Contains(Input::MousePosition);
            }

            for (auto& child : Children) {
                child->OnUpdate();
            }
        }

        virtual ControlBase* SelectFirst() {
            for (auto& child : Children) {
                if (child->Selectable) {
                    return child.get();
                }
                else if (auto control = child->SelectFirst()) {
                    return control;
                }
            }

            if (Selectable) return this;

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

        // Populates a list containing all keyboard selectable controls
        void FlattenSelectionTree(List<ControlBase*>& controls) const {
            for (auto& child : Children) {
                if (child->Selectable)
                    controls.push_back(child.get());

                child->FlattenSelectionTree(controls);
            }
        }

        Vector2 GetScreenPosition;
        List<Ptr<ControlBase>> Children;

        template <class T, class... Args>
        void AddChild(Args&&... args) {
            auto control = make_unique<T>(std::forward<Args>(args)...);
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

        string ActionSound = MENU_SELECT_SOUND;

        // Called when the control is clicked via some input device
        Action ClickAction;
    };

    class Rectangle : public ControlBase {
    public:
        Rectangle() {
            Selectable = false;
        }

        Color Fill;

        void OnDraw() override {
            Render::CanvasBitmapInfo cbi;
            cbi.Position = ScreenPosition;
            cbi.Size = ScreenSize;
            cbi.Texture = Render::Materials->White().Handle();
            cbi.Color = Fill;
            Render::UICanvas->DrawBitmap(cbi, Layer);
        }
    };

    class Label : public ControlBase {
        string _text;
        FontSize _font;

    public:
        Color Color = { 1, 1, 1 };

        Label(string_view text, FontSize font = FontSize::Medium) : _text(text), _font(font) {
            Selectable = false;
        }

        void OnUpdateLayout() override {
            Size = MeasureString(_text, _font);
        }

        void OnDraw() override {
            Render::DrawTextInfo dti;
            dti.Font = _font;
            dti.Color = Color;
            dti.Position = ScreenPosition / GetScale() + Margin;
            Render::UICanvas->DrawText(_text, dti, Layer);
        }
    };

    // A listbox contains a stack panel of items, but only a certain number are visible at once
    class ListBox : public ControlBase {
        //StackPanel* _list;
        FontSize _font;
        float _fontHeight = 0;
        int _index = 0;
        int _scrollIndex = 0; // top of the list

        static constexpr float LINE_OFFSET = 1; // correction for line height
    public:
        int VisibleItems = 5;
        float ItemSpacing = 2;

        void SetIndex(int index) {
            _index = index;
        }

        List<string> Items;

        std::function<void(int)> ClickItemAction;

        ListBox(int visibleItems) : VisibleItems(visibleItems) {
            _fontHeight = MeasureString("Descent", FontSize::Medium).y;
            AddChild(make_unique<Rectangle>());
            Padding = Vector2(2, 2);
            ClickAction = [this] {
                if (ClickItemAction) ClickItemAction(_index);
            };
        }

        void OnUpdate() override {
            if (!Focused) return;

            using Input::Keys;

            if (Input::IsKeyPressed(Keys::PageDown, true)) {
                _index += VisibleItems;
                if (_scrollIndex + VisibleItems < Items.size())
                    _scrollIndex += VisibleItems;
            }

            if (Input::IsKeyPressed(Keys::PageUp, true)) {
                _index -= VisibleItems;
                _scrollIndex -= VisibleItems;
            }

            auto wheelDelta = Input::GetWheelDelta();
            _scrollIndex -= wheelDelta / 40;

            if (Input::IsKeyPressed(Keys::Up, true)) {
                _index--;
                if (_index < _scrollIndex)
                    _scrollIndex = _index;
            }

            if (Input::IsKeyPressed(Keys::Down, true)) {
                _index++;
                if (_index > _scrollIndex + VisibleItems - 1)
                    _scrollIndex++;
            }

            if (Items.size() <= VisibleItems) {
                _scrollIndex = 0; // Reset scrolling if all items fit on screen
            }

            bool clicked = Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick);

            if (clicked && ClickItemAction && HitTestCursor()) {
                ClickItemAction(_index);
            }

            _index = std::clamp(_index, 0, (int)Items.size() - 1);
            _scrollIndex = std::clamp(_scrollIndex, 0, std::max((int)Items.size() - VisibleItems, 0));

            if (wheelDelta != 0)
                HitTestCursor(); // Update index when scrolling

            //if (Input::IsKeyPressed(Keys::Enter) && ClickItemAction) {
            //    ClickItemAction(_index);
            //}
        }

        ControlBase* HitTestCursor() override {
            auto scale = GetScale();
            const float rowHeight = (_fontHeight + ItemSpacing) * scale;

            for (int i = _scrollIndex, j = 0; i < Items.size() && i < _scrollIndex + VisibleItems; i++, j++) {
                auto position = ScreenPosition;
                position.y += rowHeight * j;
                Vector2 size(ScreenSize.x, rowHeight);

                if (RectangleContains(position, size, Input::MousePosition)) {
                    _index = i;
                    break;
                }
            }

            if (RectangleContains(ScreenPosition, ScreenSize, Input::MousePosition))
                return this;
            else
                return nullptr;
        }

        void OnUpdateLayout() override {
            Size.y = _fontHeight * VisibleItems + ItemSpacing * (VisibleItems - 1);
            ControlBase::OnUpdateLayout();
        }

        void OnDraw() override {
            {
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition;
                cbi.Size = ScreenSize;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Color(0, 0, 0, 1);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            for (int i = _scrollIndex, j = 0; i < Items.size() && i < _scrollIndex + VisibleItems; i++, j++) {
                auto& item = Items[i];

                Render::DrawTextInfo dti;
                dti.Font = _index == i ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = _index == i ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position = ScreenPosition / GetScale() + Padding;
                dti.Position.y += (_fontHeight + ItemSpacing) * j + LINE_OFFSET;
                Render::UICanvas->DrawText(item, dti, Layer);
            }


            // Draw scrollbar
            if (!Items.empty()) {
                float percentVisible = (float)VisibleItems / Items.size();
                if (percentVisible < 1) {
                    const float scrollWidth = 3 * GetScale();
                    const float scrollHeight = ScreenSize.y * percentVisible;
                    float percent = (float)_scrollIndex / (Items.size() - VisibleItems);
                    float offset = (ScreenSize.y - scrollHeight) * percent;

                    Render::CanvasBitmapInfo cbi;
                    cbi.Position = Vector2(ScreenPosition.x + ScreenSize.x - scrollWidth, ScreenPosition.y + offset);
                    cbi.Size = Vector2(scrollWidth, scrollHeight);
                    cbi.Texture = Render::Materials->White().Handle();
                    cbi.Color = ACCENT_COLOR;
                    Render::UICanvas->DrawBitmap(cbi, Layer + 1);
                }
            }
        }
    };


    class Button : public ControlBase {
        string _text;
        AlignH _alignment;
        Vector2 _textSize;

    public:
        Color TextColor = Color(1, 1, 1);
        Color FocusColor = FOCUS_COLOR;

        Button(string_view text, AlignH alignment = AlignH::Left) : _text(text), _alignment(alignment) {
            _textSize = Size = MeasureString(_text, FontSize::Medium);
            Selectable = true;
            Padding = Vector2{ 2, 2 };
        }

        Button(string_view text, Action&& action, AlignH alignment = AlignH::Left) : _text(text), _alignment(alignment) {
            ClickAction = action;
            _textSize = Size = MeasureString(_text, FontSize::Medium);
            Padding = Vector2{ 2, 2 };
        }

        void OnDraw() override {
            Render::DrawTextInfo dti;
            dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
            dti.Color = Focused /*|| Hovered*/ ? FocusColor : TextColor;
            dti.Position = ScreenPosition / GetScale() + Padding;

            if (_alignment == AlignH::Center) {
                dti.Position.x += Size.x / 2 - _textSize.x / 2;
            }
            else if (_alignment == AlignH::Right) {
                dti.Position.x += Size.x - _textSize.x;
            }

            Render::UICanvas->DrawText(_text, dti, Layer);
        }
    };

    class CloseButton : public ControlBase {
    public:
        CloseButton(Action&& action) {
            ClickAction = action;
            Size = Vector2(15, 15);
            Selectable = false; // Disable keyboard navigation
            ActionSound = MENU_BACK_SOUND;
        }

        float Thickness = 1.0f;

        void OnDraw() override {
            const float thickness = Thickness * GetScale();

            Render::HudCanvasPayload payload;
            payload.Texture = Render::Materials->White().Handle();
            payload.Layer = Layer;
            payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = Focused || Hovered ? ACCENT_GLOW : IDLE_BUTTON;

            float size = ScreenSize.x;
            auto position = ScreenPosition;

            // tl to br
            payload.V0.Position = position;
            payload.V1.Position = Vector2(position.x + thickness, position.y);
            payload.V2.Position = Vector2(position.x + size, position.y + size - thickness);
            payload.V3.Position = Vector2(position.x + size, position.y + size);
            Render::UICanvas->Draw(payload);

            payload.V0.Position = position;
            payload.V1.Position = Vector2(position.x, position.y + thickness);
            payload.V2.Position = Vector2(position.x + size - thickness, position.y + size);
            payload.V3.Position = Vector2(position.x + size, position.y + size);
            Render::UICanvas->Draw(payload);

            // tr to bl
            payload.V0.Position = Vector2(position.x + size, position.y);
            payload.V1.Position = Vector2(position.x + size - thickness, position.y);
            payload.V2.Position = Vector2(position.x, position.y + size - thickness);
            payload.V3.Position = Vector2(position.x, position.y + size);
            payload.Layer = Layer;
            Render::UICanvas->Draw(payload);

            payload.V0.Position = Vector2(position.x + size, position.y);
            payload.V1.Position = Vector2(position.x + size, position.y + thickness);
            payload.V2.Position = Vector2(position.x + thickness, position.y + size);
            payload.V3.Position = Vector2(position.x, position.y + size);
            payload.Layer = Layer;
            Render::UICanvas->Draw(payload);
        }
    };

    enum class PanelOrientation { Horizontal, Vertical };

    class StackPanel : public ControlBase {
    public:
        StackPanel() { Selectable = false; }

        PanelOrientation Orientation = PanelOrientation::Vertical;
        int Spacing = 0;

        void OnUpdateLayout() override {
            auto anchor = Render::GetAlignment(Size, HorizontalAlignment, VerticalAlignment, Render::UICanvas->GetSize() / Render::UICanvas->GetScale());

            if (Orientation == PanelOrientation::Vertical) {
                //float maxWidth = 0;
                float maxLayoutWidth = 0;
                float yOffset = anchor.y;

                for (auto& child : Children) {
                    child->Position.y = child->Margin.y + yOffset;
                    child->Position.x = child->Margin.x;
                    child->UpdateScreenPosition(*this);
                    child->Layer = Layer;
                    child->OnUpdateLayout();

                    auto layoutWidth = child->MeasureWidth();

                    maxLayoutWidth = std::max(maxLayoutWidth, layoutWidth);
                    //maxWidth = std::max(child->Size.x, maxWidth);
                    yOffset += child->Size.y + child->Margin.y * 2 + child->Padding.y * 2 + Spacing;
                }

                // Expand to parent container
                maxLayoutWidth = std::max(maxLayoutWidth, Size.x);

                // Expand children to max width to make clicking uniform
                for (auto& child : Children)
                    if (child->DockFill)
                        child->Size.x = maxLayoutWidth - child->Margin.x * 2 - child->Padding.x * 2;
                
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
                    child->Layer = Layer;
                    child->OnUpdateLayout();

                    if (child->Size.y > maxHeight)
                        maxHeight = child->Size.y;

                    if (child->Margin.x > maxMargin)
                        maxMargin = child->Margin.x;

                    xOffset += child->Size.x + child->Margin.x * 2 + child->Padding.x * 2/* + Spacing*/;
                }

                // Expand children to max height to make clicking uniform
                for (auto& child : Children)
                    if (child->DockFill)
                        child->Size.y = maxHeight;

                Size = Vector2(xOffset, maxHeight);
            }
        }
    };


    class Spinner : public ControlBase {
        string _text = "0";
        gsl::strict_not_null<int*> _value;
        int _min = 0, _max = 10;

        bool _held = false;
        float _holdTimer = 0;
        static constexpr float REPEAT_SPEED = 0.075f; // how quickly the repeat happens
        static constexpr float REPEAT_DELAY = 0.5f; // how long mouse must be held to repeat add 

    public:
        Color TextColor = Color(1, 1, 1);
        Color FocusColor = FOCUS_COLOR;

        Spinner(int min, int max, int& value) : _value(&value) {
            Size = Vector2(100, 20);
            Padding = Vector2(4, 4);
            DockFill = false;
            SetRange(min, max);
            SetValue(min);
        }

        void SetValue(int value) {
            if (value == *_value) return;
            *_value = std::clamp(value, _min, _max);
            _text = std::to_string(*_value);
        }

        void SetRange(int min, int max) {
            if (min > max) std::swap(min, max);
            _min = min;
            _max = max;
            *_value = std::clamp(*_value, _min, _max);
            _text = std::to_string(*_value);
        }

        void OnUpdate() override {
            if (!Focused) return;

            int increment = 0;
            int mult = Input::ShiftDown ? 10 : 1;

            if (Input::IsKeyPressed(Input::Keys::Left, true))
                increment = -1;

            if (Input::IsKeyPressed(Input::Keys::Right, true))
                increment = 1;

            auto wheelDelta = Input::GetWheelDelta();
            if (wheelDelta > 0) increment = 1;
            if (wheelDelta < 0) increment = -1;

            // if clicked or the mouse is held down
            if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick) || Input::IsMouseButtonDown(Input::MouseButtons::LeftClick)) {
                // This duplicates the rendering logic
                auto scale = GetScale();
                const float size = 15 * scale;
                const float buttonPadding = (ScreenSize.y - size) / 2;

                {
                    // subtract
                    auto position = ScreenPosition;
                    position.x += buttonPadding;
                    position.y += buttonPadding;

                    if (RectangleContains(position, { size, size }, Input::MousePosition)) {
                        if (!_held) {
                            increment = -1; // first click
                            _holdTimer = REPEAT_DELAY;
                        }
                        else {
                            if (_holdTimer <= 0) {
                                increment = -1;
                                _holdTimer = REPEAT_SPEED;
                            }
                        }

                        _held = true;
                    }
                }

                {
                    // add
                    auto position = ScreenPosition;
                    position.x += ScreenSize.x - buttonPadding - size;
                    position.y += buttonPadding;

                    if (RectangleContains(position, { size, size }, Input::MousePosition)) {
                        if (!_held) {
                            increment = 1; // first click
                            _holdTimer = REPEAT_DELAY;
                        }
                        else {
                            if (_holdTimer <= 0) {
                                increment = 1;
                                _holdTimer = REPEAT_SPEED;
                            }
                        }

                        _held = true;
                    }
                }

                if (Input::IsMouseButtonReleased(Input::MouseButtons::LeftClick)) {
                    _held = false;
                }

                _holdTimer -= Inferno::Clock.GetFrameTimeSeconds();
            }

            if (increment != 0) {
                SetValue(*_value + increment * mult);
            }
        }

        void OnDraw() override {
            auto scale = GetScale();

            {
                // Border
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition;
                cbi.Size = ScreenSize;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Focused ? ACCENT_COLOR : BORDER_COLOR;
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Background
                Render::CanvasBitmapInfo cbi;
                //cbi.Position = ScreenPosition;
                //cbi.Size = ScreenSize;
                const auto border = Vector2(1, 1) * scale;
                cbi.Position = ScreenPosition + border;
                cbi.Size = ScreenSize - border * 2;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Color(0, 0, 0, 1);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            //const auto screenPadding = Padding * scale;
            const float thickness = 1 * scale;
            const float size = 15 * scale;
            float half = size / 2;
            const float buttonPadding = (ScreenSize.y - size) / 2;

            // minus
            {
                Render::HudCanvasPayload payload;
                payload.Texture = Render::Materials->White().Handle();
                payload.Layer = Layer;

                auto position = ScreenPosition;
                position.x += buttonPadding;
                position.y += buttonPadding;

                auto color = RectangleContains(position, { size, size }, Input::MousePosition) ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
                payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = color;

                // left to right
                payload.V0.Position = Vector2(position.x, position.y + half - thickness);
                payload.V1.Position = Vector2(position.x, position.y + half + thickness);
                payload.V2.Position = Vector2(position.x + size, position.y + half + thickness);
                payload.V3.Position = Vector2(position.x + size, position.y + half - thickness);
                Render::UICanvas->Draw(payload);
            }

            {
                // plus
                Render::HudCanvasPayload payload;
                payload.Texture = Render::Materials->White().Handle();
                payload.Layer = Layer;

                auto position = ScreenPosition;
                position.x += ScreenSize.x - buttonPadding - size;
                position.y += buttonPadding;

                auto color = RectangleContains(position, { size, size }, Input::MousePosition) ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
                payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = color;

                // left to right
                payload.V0.Position = Vector2(position.x, position.y + half - thickness);
                payload.V1.Position = Vector2(position.x, position.y + half + thickness);
                payload.V2.Position = Vector2(position.x + size, position.y + half + thickness);
                payload.V3.Position = Vector2(position.x + size, position.y + half - thickness);
                Render::UICanvas->Draw(payload);

                // top to bottom
                payload.V0.Position = Vector2(position.x + half - thickness, position.y);
                payload.V1.Position = Vector2(position.x + half + thickness, position.y);
                payload.V2.Position = Vector2(position.x + half + thickness, position.y + size);
                payload.V3.Position = Vector2(position.x + half - thickness, position.y + size);
                Render::UICanvas->Draw(payload);
            }

            {
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FocusColor : TextColor;
                dti.Position = ScreenPosition / scale + Padding;
                auto textLen = MeasureString(_text, FontSize::Medium).x;
                dti.Position.x += ScreenSize.x / 2 / scale - textLen / 2 - Padding.x; // center justify text
                dti.Position.y += 1; // offset from top slightly
                //dti.Position.x += ScreenSize.x / scale - textLen - Padding.x - Margin.x - size * 1.75f / scale; // right justify text
                //dti.HorizontalAlign = AlignH::Center;
                Render::UICanvas->DrawText(_text, dti, Layer + 1);
            }
        }
    };

    class Slider : public ControlBase {
        string _label;
        gsl::strict_not_null<int*> _value;
        bool _held = false;
        string _valueText;
        float _barPadding = 10;
        bool _dragging = false;

    public:
        Slider(string_view label, int min, int max, int& value) : _label(label), _value(&value), Min(min), Max(max) {
            auto textSize = MeasureString(_label, FontSize::Medium);
            Size = Vector2(60, textSize.y);
            BarOffset = textSize.x + _barPadding;
            UpdateValueText();
        }

        void UpdateValueText() {
            _valueText = std::to_string(*_value);
        }

        int Min, Max;
        float BarOffset = 0;
        float ValueWidth = 25;
        string ChangeSound; // MENU_SELECT_SOUND
        bool ShowValue = false;

        //void Clicked(const Vector2& position) {
        //    //if (!Contains(position)) return;
        //    // determine the location within the slider and set the value
        //}

        std::function<void(int)> OnChange;

        void UpdatePercent(float percent) {
            auto value = (int)std::floor((Max - Min) * percent);
            if (*_value != value) {
                *_value = value;
                if (OnChange) OnChange(value);
                Sound::Play2D(ChangeSound, 1, 0, 0.25f);
                UpdateValueText();
            }
        }

        float GetValueWidth() const { return ShowValue ? ValueWidth : 0; }

        void OnUpdate() override {
            // behavior:
            // when clicked, move cursor to nearest increment, and then continue to update as dragged
            // need a global flag to tell focus system to not update while dragging

            if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick) && CheckHover()) {
                _dragging = true;
            }
            else if (!Input::IsMouseButtonDown(Input::MouseButtons::LeftClick)) {
                _dragging = false;
            }

            if (_dragging) {
                auto barWidth = (Size.x - BarOffset - GetValueWidth() - _barPadding) * GetScale();
                auto barPosition = ScreenPosition.x + BarOffset * GetScale();
                auto tickWidth = barWidth / (Max - Min);

                auto percent = Saturate((Input::MousePosition.x - barPosition + tickWidth / 2) / barWidth);
                UpdatePercent(percent);
            }
        }

        bool CheckHover() {
            auto barWidth = (Size.x - BarOffset - GetValueWidth() - _barPadding) * GetScale();
            auto barPosition = Vector2(ScreenPosition.x + BarOffset * GetScale(), ScreenPosition.y);
            return RectangleContains(barPosition, { barWidth, ScreenSize.y }, Input::MousePosition);
        }

        void OnDraw() override {
            //{
            //    // Border
            //    Render::CanvasBitmapInfo cbi;
            //    cbi.Position = ScreenPosition;
            //    cbi.Size = ScreenSize;
            //    cbi.Texture = Render::Materials->White().Handle();
            //    cbi.Color = Focused ? ACCENT_COLOR : BORDER_COLOR;
            //    Render::UICanvas->DrawBitmap(cbi, Layer);
            //}

            //{
            //    // Background
            //    Render::CanvasBitmapInfo cbi;
            //    //cbi.Position = ScreenPosition;
            //    //cbi.Size = ScreenSize;
            //    const auto border = Vector2(1, 1) * scale;
            //    cbi.Position = ScreenPosition + border;
            //    cbi.Size = ScreenSize - border * 2;
            //    cbi.Texture = Render::Materials->White().Handle();
            //    cbi.Color = Color(0, 0, 0, 1);
            //    Render::UICanvas->DrawBitmap(cbi, Layer);
            //}

            auto hovered = _dragging || CheckHover();
            auto barWidth = (Size.x - BarOffset - GetValueWidth() - _barPadding) * GetScale();
            auto barPosition = Vector2(ScreenPosition.x + BarOffset * GetScale(), ScreenPosition.y - 1 * GetScale());
            auto percent = GetPercent();

            {
                // Bar (left half)
                //const float barHeight = ScreenSize.y * 0.5f;
                const float barHeight = 6 * GetScale();

                Render::CanvasBitmapInfo cbi;
                //cbi.Position = ScreenPosition;
                //cbi.Size = ScreenSize;
                //const auto border = Vector2(1, 1) * scale;
                cbi.Position = barPosition;
                cbi.Position.y += ScreenSize.y / 2 - barHeight / 2;
                //cbi.Position.y += ScreenSize.y / 2 - barHeight * GetScale() / 2;
                cbi.Size = Vector2(barWidth * percent, barHeight);
                cbi.Texture = Render::Materials->White().Handle();
                //auto color = hovered ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
                auto color = hovered ? ACCENT_GLOW : Focused ? Color(246.0f / 255, 153.0f / 255, 66.0f / 255) : IDLE_BUTTON;
                cbi.Color = color * 0.8f;
                Render::UICanvas->DrawBitmap(cbi, Layer + 1);
            }

            {
                // Bar (right half)
                const float barHeight = 2 * GetScale();
                //const float barHeight = ScreenSize.y * 0.5f;
                Render::CanvasBitmapInfo cbi;
                //cbi.Position = ScreenPosition;
                //cbi.Size = ScreenSize;
                //const auto border = Vector2(1, 1) * scale;
                cbi.Position = barPosition;
                //cbi.Position.y += ScreenSize.y / 2 - barHeight * GetScale() / 2;
                cbi.Position.y += ScreenSize.y / 2 - barHeight / 2;
                cbi.Position.x += barWidth * percent;
                cbi.Size = Vector2(barWidth * (1 - percent), barHeight);
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = hovered ? FOCUS_COLOR : Focused ? HOVER_COLOR : IDLE_BUTTON;
                cbi.Color *= 0.75f;
                Render::UICanvas->DrawBitmap(cbi, Layer + 1);
            }

            {
                // Notch
                //const float notchWidth = 3;
                //const float notchHeight = 10;

                auto color = hovered ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
                const float notchHeight = 20 * GetScale();
                float notchWidth = 8 * GetScale();

                //Render::CanvasBitmapInfo cbi;
                //cbi.Position = ScreenPosition;
                //cbi.Size = ScreenSize;
                //const auto border = Vector2(1, 1) * scale;
                auto position = barPosition;
                position.x += (barWidth - notchWidth) * percent;
                position.y += ScreenSize.y / 2 - notchHeight / 2;

                //cbi.Position = barPosition;
                //cbi.Position.x += barWidth * percent;
                //cbi.Position.y += ScreenSize.y / 2 - notchHeight / 2;
                ////cbi.Position.y += ScreenSize.y / 2 /*- notchHeight * GetScale() / 2*/;
                //cbi.Size = Vector2(3 * GetScale(), notchHeight);
                //cbi.Texture = Render::Materials->White().Handle();
                //cbi.Color = color;
                //Render::UICanvas->DrawBitmap(cbi, Layer + 1);


                Render::HudCanvasPayload payload;
                payload.Texture = Render::Materials->White().Handle();
                payload.Layer = Layer + 1;
                payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = color;

                //float size = ScreenSize.x;
                //auto position = ScreenPosition;

                payload.V0.Position = position;
                payload.V1.Position = Vector2(position.x + notchWidth, position.y + notchWidth);
                payload.V2.Position = Vector2(position.x + notchWidth, position.y + notchHeight);
                payload.V3.Position = Vector2(position.x, position.y + notchHeight);
                Render::UICanvas->Draw(payload);
            }

            {
                // Label
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                //dti.Position = ScreenPosition / GetScale();
                dti.Position = ScreenPosition / GetScale();
                //dti.Position.x += Size.x + 5;
                //auto textLen = MeasureString(_text, FontSize::Medium).x;
                //dti.Position.x += ScreenSize.x / 2 / scale - textLen / 2 - Padding.x; // center justify text
                //dti.Position.y += 1; // offset from top slightly
                //dti.Position.x += ScreenSize.x / scale - textLen - Padding.x - Margin.x - size * 1.75f / scale; // right justify text
                //dti.HorizontalAlign = AlignH::Center;
                Render::UICanvas->DrawText(_label, dti, Layer + 1);
            }


            if (ShowValue) {
                // Value
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position = Vector2(ScreenPosition.x + ScreenSize.x - ValueWidth * GetScale(), ScreenPosition.y) / GetScale();

                //dti.Position.x += BarOffset + Size.x + 5;
                //auto textLen = MeasureString(_text, FontSize::Medium).x;
                //dti.Position.x += ScreenSize.x / 2 / scale - textLen / 2 - Padding.x; // center justify text
                //dti.Position.y += 1; // offset from top slightly
                //dti.Position.x += ScreenSize.x / scale - textLen - Padding.x - Margin.x - size * 1.75f / scale; // right justify text
                //dti.HorizontalAlign = AlignH::Center;
                Render::UICanvas->DrawText(_valueText, dti, Layer + 1);
            }
        }

    private:
        float GetPercent() const {
            return float(*_value - Min) / float(Max - Min);
        }
    };


    bool CloseScreen();

    enum class CloseState { None, Accept, Cancel };

    class ScreenBase : public ControlBase {
    public:
        ScreenBase() {
            Selectable = false;
            Padding = Vector2(5, 5);
        }

        bool CloseOnConfirm = true;
        CloseState State = CloseState::None;

        ControlBase* Selection = nullptr;
        ControlBase* LastGoodSelection = nullptr;
        std::function<void(CloseState)> CloseCallback;

        void OnUpdate() override {
            if (Input::MouseMoved()) {
                // Update selection when cursor moves, but only if the control is valid
                if (auto control = HitTestCursor())
                    SetSelection(control);
            }

            ControlBase::OnUpdate(); // breaks main menu selection
        }

        // Called when a top level screen tries to close. Return true if it should close.
        virtual bool OnTryClose() { return false; }

        // Called when a screen is closed.
        virtual void OnClose() {}

        void OnConfirm() {
            if (Selection && Selection->ClickAction) {
                Sound::Play2D(SoundResource{ ActionSound });
                Selection->ClickAction();
            }
            else if (CloseOnConfirm) {
                // Play the default menu select sound when closing if there's no action
                Sound::Play2D(SoundResource{ MENU_SELECT_SOUND });
            }

            if (CloseOnConfirm)
                State = CloseState::Accept;
        }

        void OnUpdateLayout() override {
            // Fill the whole screen if the size is zero
            auto& canvasSize = Render::UICanvas->GetSize();
            ScreenSize = Size == Vector2::Zero ? canvasSize : Size * GetScale();
            ScreenPosition = Render::GetAlignment(ScreenSize, HorizontalAlignment, VerticalAlignment, canvasSize);
            ControlBase::OnUpdateLayout();
        }

        void SetSelection(ControlBase* control) {
            if (Selection) Selection->Focused = false;
            Selection = control;

            if (control) {
                control->Focused = true;
                LastGoodSelection = Selection;
            }
        }

        ControlBase* SelectFirst() override {
            for (auto& control : Children) {
                Selection = control->SelectFirst();
                if (Selection) {
                    SetSelection(Selection);
                    break;
                }
            }

            return Selection;
        }

        // Returns -1 if the selection is not found
        int FindSelectionIndex(const List<ControlBase*>& tree) const {
            for (int i = 0; i < (int)tree.size(); i++) {
                if (Selection == tree[i])
                    return i;
            }

            return -1;
        }

        void OnUpArrow() {
            List<ControlBase*> tree;
            FlattenSelectionTree(tree);
            int index = FindSelectionIndex(tree);

            if (index == 0 || index == -1)
                SetSelection(tree.back()); // wrap
            else
                SetSelection(tree[index - 1]);
        }

        void OnDownArrow() {
            List<ControlBase*> tree;
            FlattenSelectionTree(tree);
            int index = FindSelectionIndex(tree);

            if (index == tree.size() - 1 || index == -1)
                SetSelection(tree.front()); // wrap
            else
                SetSelection(tree[index + 1]);
        }
    };

    class DialogBase : public ScreenBase {
    public:
        DialogBase(string_view title = "", bool showCloseButton = true) {
            HorizontalAlignment = AlignH::Center;
            VerticalAlignment = AlignV::Center;

            if (showCloseButton) {
                auto close = make_unique<CloseButton>([this] { OnDialogClose(); });
                close->HorizontalAlignment = AlignH::Right;
                close->Margin = Vector2(DIALOG_PADDING, DIALOG_PADDING);
                AddChild(std::move(close));
            }

            if (!title.empty()) {
                auto titleLabel = make_unique<Label>(title, FontSize::MediumBlue);
                titleLabel->VerticalAlignment = AlignV::Top;
                titleLabel->HorizontalAlignment = AlignH::Center;
                titleLabel->Position = Vector2(0, DIALOG_PADDING);
                titleLabel->Color = DIALOG_TITLE_COLOR;
                AddChild(std::move(titleLabel));
            }
        }

        virtual void OnDialogClose() {
            CloseScreen();
        }

        void OnDraw() override {
            const auto border = Vector2(1, 1) * GetScale();

            {
                // Border
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition;
                cbi.Size = ScreenSize;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = BORDER_COLOR;
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Background
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition + border;
                cbi.Size = ScreenSize - border * 2;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = DIALOG_BACKGROUND;
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            //{
            //    // Header
            //    Render::CanvasBitmapInfo cbi;
            //    cbi.Position = ScreenPosition + border;
            //    cbi.Size = Vector2(ScreenSize.x - border.x * 2, 30 * GetScale());
            //    cbi.Texture = Render::Materials->White().Handle();
            //    cbi.Color = Color(0.02f, 0.02f, 0.02f, 1);
            //    Render::UICanvas->DrawBitmap(cbi, 1);
            //}

            //{
            //    Render::DrawTextInfo dti;
            //    dti.Font = FontSize::Big;
            //    dti.HorizontalAlign = AlignH::Center;
            //    dti.VerticalAlign = AlignV::Top;
            //    dti.Position = Vector2(0, 20);
            //    dti.Color = Color(1, 1, 1, 1);
            //    Render::UICanvas->DrawGameText("Header", dti, 2);
            //}

            ScreenBase::OnDraw();
        }
    };
}
