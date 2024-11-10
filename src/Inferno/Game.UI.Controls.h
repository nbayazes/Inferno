#pragma once
#include "Types.h"
#include "Game.Text.h"
#include "Graphics/Render.h"
#include "gsl/pointers.h"
#include "SoundSystem.h"
#include "Input.h"

namespace Inferno {
    constexpr auto MENU_SELECT_SOUND = "data/menu-select3.wav";
    constexpr auto MENU_BACK_SOUND = "data/menu-back1.wav";
    constexpr Color HOVER_COLOR = { 1, .9f, 0.9f };
    const auto FOCUS_COLOR = HOVER_COLOR * 1.7f;
    constexpr Color ACCENT_COLOR = { 1, .75f, .2f };
    const auto ACCENT_GLOW = ACCENT_COLOR * 2.0f;
    constexpr Color BORDER_COLOR = { 0.25f, 0.25f, 0.25f };
    constexpr Color IDLE_BUTTON = { 0.4f, 0.4f, 0.4f };
    constexpr Color DIALOG_TITLE_COLOR = { 1.25f, 1.25f, 2.0f };
    constexpr Color DIALOG_BACKGROUND = { 0.1f, 0.1f, 0.1f };
    constexpr Color HELP_TEXT_COLOR = { 0.75f, 0.75f, 0.75f };
    constexpr float DIALOG_MARGIN = 15;

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
                for (auto& child : Children) {
                    SelectionState state = child->SelectPrevious(current);

                    // check wrap around
                    if (state.SelectPrev)
                        return { Children.back()->SelectLast() };

                    if (state.Selection)
                        return state;
                }
            }

            return { nullptr };
        }

        // Populates a list containing all keyboard selectable controls
        void FlattenSelectionTree(List<ControlBase*>& controls) const {
            for (auto& child : Children) {
                if (child->Selectable)
                    controls.push_back(child.get());

                child->FlattenSelectionTree(controls);
            }
        }

        SelectionState SelectNext(ControlBase* current) const {
            int index = -1;

            for (size_t i = 0; i < Children.size(); i++) {
                if (Children[i].get() == current) {
                    index = (int)i;
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
                if(ClickItemAction) ClickItemAction(_index);
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

    public:
        Color TextColor = Color(1, 1, 1);
        Color FocusColor = FOCUS_COLOR;

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
            dti.Color = Focused /*|| Hovered*/ ? FocusColor : TextColor;
            dti.Position = ScreenPosition / GetScale() + Padding;
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
        //int Spacing = 2;

        void OnUpdateLayout() override {
            auto anchor = Render::GetAlignment(Size, HorizontalAlignment, VerticalAlignment, Render::UICanvas->GetSize() / Render::UICanvas->GetScale());

            if (Orientation == PanelOrientation::Vertical) {
                float maxWidth = 0;
                float maxLayoutWidth = 0;
                float yOffset = anchor.y;

                for (auto& child : Children) {
                    child->Position.y = child->Margin.y + yOffset;
                    child->Position.x = child->Margin.x;
                    child->UpdateScreenPosition(*this);
                    child->Layer = Layer;
                    child->OnUpdateLayout();

                    auto width = child->MeasureWidth();
                    if (maxLayoutWidth < width) maxLayoutWidth = width;
                    if (child->Size.x > maxWidth)
                        maxWidth = child->Size.x;

                    //if (child->Margin.x > maxMargin)
                    //    maxMargin = child->Margin.x;

                    yOffset += child->Size.y + child->Margin.y * 2 + child->Padding.y * 2/* + Spacing*/;
                }

                // Expand children to max width to make clicking uniform
                for (auto& child : Children)
                    if (child->DockFill)
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
}
