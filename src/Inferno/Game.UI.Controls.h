#pragma once
#include <algorithm>
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
    constexpr Color BLUE_TEXT = { 0.5f, 0.5f, 1.0f };
    constexpr Color WHITE_TEXT = { 0.95f, 0.95f, 0.95f };
    constexpr Color FOCUSED_BUTTON = { 246.0f / 255, 153.0f / 255, 66.0f / 255 };
    constexpr float DIALOG_PADDING = 15;
    constexpr float DIALOG_CONTENT_PADDING = DIALOG_PADDING + 30;
    constexpr float MENU_TEXT_HEIGHT = 24; // Medium high res font
    constexpr float CONTROL_PADDING = 1;
    constexpr float CONTROL_HEIGHT = MENU_TEXT_HEIGHT + CONTROL_PADDING * 2;
    constexpr float SMALL_CONTROL_HEIGHT = 10 + CONTROL_PADDING * 2;

    // Prevents focus from changing when true. Call with false to release
    void CaptureCursor(bool);
    bool IsCursorCaptured();

    // Prevents all default navigation when true
    void CaptureInput(bool capture);
    bool IsInputCaptured();

    void SetSelection(class ControlBase* control);

    using Action = std::function<void()>;

    inline float GetScale() {
        return Render::UICanvas->GetScale();
    }

    // Returns true if a rectangle at a position and size contain a point
    inline bool RectangleContains(const Vector2 origin, const Vector2& size, const Vector2& point) {
        if (IsCursorCaptured()) return false;

        return
            point.x > origin.x && point.x < origin.x + size.x &&
            point.y > origin.y && point.y < origin.y + size.y;
    }

    // Same as RectangleContains, but checks if the cursor is captured
    //inline bool RectangleContainsEx(const Vector2 origin, const Vector2& size, const Vector2& point) {
    //    if(IsCursorCaptured()) return false;
    //    return RectangleContains(origin, size, point);
    //}

    // Controls are positioned at their top left corner
    class ControlBase {
    public:
        ControlBase() = default;
        ControlBase(const ControlBase&) = delete;
        ControlBase(ControlBase&&) = default;
        ControlBase& operator=(const ControlBase&) = delete;
        ControlBase& operator=(ControlBase&&) = default;
        virtual ~ControlBase() = default;

        ControlBase* Parent = nullptr;

        bool Selectable = true; // Able to be selected by keyboard, mouse, or gamepad input. Mouse hover effects can still work.
        bool Focused = false; // Focused by the screen and should process input
        bool Hovered = false; // Mouse cursor is over the control
        bool Enabled = true;
        bool Visible = true; // Disables all events and rendering
        bool IsMouseOver = false; // True when the cursor is over the control

        bool DockFill = true; // Match size of parent layout container
        bool IsContainer = false; // Enables special input handling like scrolling

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

        // Returns the index of a child control
        Option<int> FindChildIndex(const ControlBase* control) const {
            for (int i = 0; i < Children.size(); i++) {
                if (control == Children[i].get())
                    return { i };
            }

            return {};
        }

        virtual void OnUpdateLayout() {
            if (!Visible) return;

            // Arrange children relative to this control
            for (auto& control : Children) {
                control->UpdateScreenPosition(*this);
                control->Layer = Layer + 1;
                control->OnUpdateLayout();
            }
        }

        void UpdateScreenPosition(const ControlBase& parent) {
            if (!Visible) return;

            auto scale = Render::UICanvas->GetScale();
            ScreenPosition = Position * scale + parent.ScreenPosition + Margin * scale;
            ScreenSize = Size * scale + Padding * 2 * scale;

            auto offset = Render::GetAlignment(Size * scale, HorizontalAlignment, VerticalAlignment, parent.ScreenSize, Margin * scale);
            ScreenPosition += offset;
        }

        bool Contains(const Vector2& point) const {
            if (!Visible) return false;

            return
                point.x > ScreenPosition.x && point.x < ScreenPosition.x + ScreenSize.x &&
                point.y > ScreenPosition.y && point.y < ScreenPosition.y + ScreenSize.y;
        }

        virtual ControlBase* HitTestCursor() {
            if (!Enabled || !Visible) {
                IsMouseOver = false;
                return nullptr;
            }

            IsMouseOver = Contains(Input::MousePosition);

            if (Selectable && IsMouseOver) {
                return this;
            }

            for (auto& child : Children) {
                if (auto control = child->HitTestCursor())
                    return control;
            }

            return nullptr;
        }

        void OnMouseClick(const Vector2& position) const {
            for (auto& control : Children) {
                if (control->Enabled && control->Contains(position)) {
                    if (control->OnConfirm()) return;
                }

                control->OnMouseClick(position);
            }
        }

        // Called when the control is activated by clicking or pressing a button
        virtual bool OnConfirm() {
            if (ClickAction)
                ClickAction();

            if (HandleOnClick() || ClickAction) {
                Sound::Play2D(SoundResource{ ActionSound });
                return true; // Click was handled, stop
            }

            return false;
        }

        virtual bool HandleMenuAction(Input::MenuActionState action) {
            if (action.IsSet(MenuAction::Confirm)) {
                return OnConfirm();
            }

            return false; // Not handled
        }

        // Called each frame
        virtual void OnUpdate() {
            Hovered = false;
            if (!Enabled) return;


            if ((Selectable ? Focused && Input::MouseMoved() : true) && !IsCursorCaptured()) {
                Hovered = Contains(Input::MousePosition);
            }

            for (auto& child : Children) {
                child->OnUpdate();
            }
        }

        // Called when the control is focused
        virtual void OnSelect() {}

        // Call when a child is selected
        virtual void OnChildSelected(ControlBase* /*control*/) {}

        virtual ControlBase* SelectFirst() {
            for (auto& child : Children) {
                if (child->Selectable && child->Visible) {
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
                if (child->Selectable && child->Visible) {
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
                if (child->Selectable /*&& child->Visible*/)
                    controls.push_back(child.get());

                child->FlattenSelectionTree(controls);
            }
        }

        Vector2 GetScreenPosition;
        List<Ptr<ControlBase>> Children;

        template <class T, class... Args>
        T* AddChild(Args&&... args) {
            auto control = make_unique<T>(std::forward<Args>(args)...);
            control->Parent = this;
            auto ptr = control.get();
            Children.push_back(std::move(control));
            return ptr;
        }

        void AddChild(Ptr<ControlBase> control) {
            control->Parent = this;
            Children.push_back(std::move(control));
        }

        virtual void OnDraw() {
            for (auto& child : Children) {
                if (child->Visible)
                    child->OnDraw();
            }
        }

        string ActionSound;

        // Called when the control is clicked via some input device
        Action ClickAction;

    protected:
        // Returns true if the click was handled
        virtual bool HandleOnClick() { return false; }
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
        Vector2 _textSize;

    public:
        Color Color = { 1, 1, 1 };
        AlignH TextAlignment = AlignH::Left;

        Label(string_view text, FontSize font = FontSize::Medium) : _text(text), _font(font) {
            Selectable = false;
            _textSize = MeasureString(_text, _font);
            Size = _textSize;
            Size.y = CONTROL_HEIGHT;
        }

        void OnDraw() override {
            if (_text.empty()) return;

            Render::DrawTextInfo dti;
            dti.Font = _font;
            dti.Color = Color;

            // Center text in the label
            //dti.Position.x = ScreenPosition.x + _textSize.x * GetScale() * 0.5f - _textSize.x * GetScale() / 2.0f; /* + Margin.x * GetScale()*/
            //dti.Position.y = ScreenPosition.y + ScreenSize.y / 2 - _textSize.y * GetScale() / 2.0f;
            dti.Position.x = ScreenPosition.x + Padding.x * GetScale();
            dti.Position.y = ScreenPosition.y + ScreenSize.y / 2 - _textSize.y * GetScale() / 2;

            if (TextAlignment == AlignH::Center) {
                dti.Position.x += ScreenSize.x / 2 - _textSize.x / 2 * GetScale();
            }
            else if (TextAlignment == AlignH::Right) {
                dti.Position.x += ScreenSize.x - _textSize.x * GetScale();
            }

            Render::UICanvas->DrawRaw(_text, dti, Layer);
        }
    };

    // A listbox contains a stack panel of text, but only a certain number are visible at once
    class ListBox : public ControlBase {
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
            ActionSound = MENU_SELECT_SOUND;

            ClickAction = [this] {
                if (ClickItemAction) ClickItemAction(_index);
            };
        }

        bool HandleMenuAction(Input::MenuActionState action) override {
            if (action == MenuAction::Up) {
                _index--;
                _scrollIndex = std::min(_index, _scrollIndex);
                return true;
            }
            else if (action == MenuAction::Down) {
                _index++;
                if (_index > _scrollIndex + VisibleItems - 1)
                    _scrollIndex++;
                return true;
            }

            return false;
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

            if (Items.size() <= VisibleItems) {
                _scrollIndex = 0; // Reset scrolling if all items fit on screen
            }

            bool clicked = Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick);

            if (clicked && ClickItemAction && HitTestCursor()) {
                ClickItemAction(_index);
            }

            ClampRanges();

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
            const auto scale = GetScale();

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
                dti.Position = ScreenPosition + Padding * scale;
                dti.Position.y += ((_fontHeight + ItemSpacing) * j + LINE_OFFSET) * scale;
                Render::UICanvas->DrawRaw(item, dti, Layer);
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

    private:
        void ClampRanges() {
            _index = std::clamp(_index, 0, (int)Items.size() - 1);
            _scrollIndex = std::clamp(_scrollIndex, 0, std::max((int)Items.size() - VisibleItems, 0));
        }
    };

    // A generic listbox contains a stack panel of items, but only a certain number are visible at once
    class ListBox2 : public ControlBase {
        FontSize _font;
        int _index = 0;
        int _scrollIndex = 0; // top of the list
        int _visibleItems = 10;

    public:
        float RowHeight = SMALL_CONTROL_HEIGHT + 4;

        void SetIndex(int index) { _index = index; }
        int GetIndex() const { return _index; }

        ListBox2(int visibleItems, float width = 300) : _visibleItems(visibleItems) {
            AddChild(make_unique<Rectangle>());
            Padding = Vector2(2, 2);
            ActionSound = MENU_SELECT_SOUND;
            Selectable = false;
            Size.x = width;
            Size.y = RowHeight * _visibleItems - Padding.y;
        }

        void SelectNext() {
            _index++;
            if (_index > _scrollIndex + _visibleItems - 1)
                _scrollIndex++;

            ClampRanges();
            SetSelection(Children[_index].get());
        }

        void SelectPrevious() {
            _index--;
            _scrollIndex = std::min(_index, _scrollIndex);
            ClampRanges();
            SetSelection(Children[_index].get());
        }

        void PageDown() {
            _index += _visibleItems;
            if (_scrollIndex + _visibleItems < Children.size())
                _scrollIndex += _visibleItems;

            ClampRanges();
            SetSelection(Children[_index].get());
        }

        void PageUp() {
            _index -= _visibleItems;
            _scrollIndex -= _visibleItems;
            ClampRanges();
            SetSelection(Children[_index].get());
        }

        void OnUpdate() override {
            ControlBase::OnUpdate();

            using Input::Keys;
            HitTestCursor();

            if (/*!Focused && */!IsMouseOver) {
                ClampRanges(); // clamp in case the items change while the cursor isn't over the list
                return;
            }

            if (Input::IsKeyPressed(Keys::PageDown, true)) {
                PageDown();
            }

            if (Input::IsKeyPressed(Keys::PageUp, true)) {
                PageUp();
            }

            auto wheelDelta = Input::GetWheelDelta();
            _scrollIndex -= wheelDelta / 40;

            //if (Input::MenuUp()) {
            //    SelectPrevious();
            //}

            //if (Input::MenuDown()) {
            //    SelectNext();
            //}

            if (Children.size() <= _visibleItems) {
                _scrollIndex = 0; // Reset scrolling if all items fit on screen
            }

            ClampRanges();

            //if (wheelDelta != 0)
            //    HitTestCursor(); // Update index when scrolling
        }

        void ScrollToIndex(int index) {
            if (index < _scrollIndex + 1)
                _scrollIndex = index - 1;
            else if (index >= _scrollIndex + _visibleItems - 1)
                _scrollIndex = index - _visibleItems + 2;

            ClampRanges();
        }

        void OnChildSelected(ControlBase* control) override {
            // scroll the item into view

            if (auto index = FindChildIndex(control)) {
                ScrollToIndex(*index);
            }
        }

        void OnDraw() override {
            const auto scale = GetScale();

            {
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition;
                cbi.Size = ScreenSize;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Color(0, 0, 0, 1);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            // Set offscreen items as invisible
            for (size_t i = 0; i < Children.size(); i++) {
                auto& item = Children[i];
                item->Visible = i >= _scrollIndex && i < _scrollIndex + _visibleItems;
            }

            for (int i = _scrollIndex, j = 0; i < Children.size() && i < _scrollIndex + _visibleItems; i++, j++) {
                auto& item = Children[i];

                item->ScreenPosition = ScreenPosition + Padding * scale;
                item->ScreenPosition.y += RowHeight * j * scale;
                if (item->Visible)
                    item->OnDraw();
            }

            // Draw scrollbar
            if (!Children.empty()) {
                float percentVisible = (float)_visibleItems / Children.size();
                if (percentVisible < 1) {
                    const float scrollWidth = 3 * GetScale();
                    const float scrollHeight = ScreenSize.y * percentVisible;
                    float percent = (float)_scrollIndex / (Children.size() - _visibleItems);
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

    protected:
        void ClampRanges() {
            _index = std::clamp(_index, 0, (int)Children.size() - 1);
            _scrollIndex = std::clamp(_scrollIndex, 0, std::max((int)Children.size() - _visibleItems, 0));
        }
    };

    class Button : public ControlBase {
        string _text;
        AlignH _alignment;
        Vector2 _textSize;
        FontSize _font;

    public:
        Color TextColor = Color(1, 1, 1);
        Color FocusColor = FOCUS_COLOR;

        Button(string_view text, AlignH alignment = AlignH::Left) : _text(text), _alignment(alignment), _font(FontSize::Medium) {
            _textSize = Size = MeasureString(_text, _font);
            Selectable = true;
            Padding = Vector2{ 2, 2 };
            ActionSound = MENU_SELECT_SOUND;
        }

        Button(string_view text, Action&& action, AlignH alignment = AlignH::Left, FontSize font = FontSize::Medium) : _text(text), _alignment(alignment), _font(font) {
            ClickAction = action;
            _textSize = Size = MeasureString(_text, _font);
            Padding = Vector2{ 2, 2 };
            ActionSound = MENU_SELECT_SOUND;

            if (_font == FontSize::Small)
                TextColor = BORDER_COLOR;
        }

        void OnDraw() override {
            Render::DrawTextInfo dti;

            if (_font == FontSize::Small) {
                dti.Font = FontSize::Small;
            }
            else {
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
            }

            dti.Color = Focused ? FocusColor : TextColor;

            dti.Position.x = ScreenPosition.x + Padding.x * GetScale();
            dti.Position.y = ScreenPosition.y + ScreenSize.y * 0.5f - _textSize.y * 0.5f * GetScale();

            if (_alignment == AlignH::Center) {
                dti.Position.x += ScreenSize.x / 2 - _textSize.x / 2 * GetScale() - Padding.x * GetScale();
            }
            else if (_alignment == AlignH::Right) {
                dti.Position.x += ScreenSize.x - _textSize.x * GetScale() - Padding.x * GetScale();
            }

            Render::UICanvas->DrawRaw(_text, dti, Layer);
        }
    };

    class CloseButton : public ControlBase {
    public:
        CloseButton(Action&& action) {
            ClickAction = action;
            Size = Vector2(15, 15);
            Selectable = false; // Disable keyboard navigation
            //ActionSound = MENU_BACK_SOUND;
        }

        float Thickness = 2.0f;

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
            Render::UICanvas->Draw(payload);

            payload.V0.Position = Vector2(position.x + size, position.y);
            payload.V1.Position = Vector2(position.x + size, position.y + thickness);
            payload.V2.Position = Vector2(position.x + thickness, position.y + size);
            payload.V3.Position = Vector2(position.x, position.y + size);
            Render::UICanvas->Draw(payload);
        }
    };

    class Checkbox : public ControlBase {
        gsl::strict_not_null<bool*> _value;
        string _label;
        Vector2 _textSize;

    public:
        Checkbox(string_view label, bool& value) : _value(&value), _label(label) {
            _textSize = MeasureString(label, FontSize::Medium);
            Size.x = _textSize.x + CheckboxSize + CheckboxPadding;
            Size.y = CONTROL_HEIGHT;
            ActionSound = MENU_SELECT_SOUND;
        }

        float Thickness = 2.0f;
        float CheckboxSize = 18;
        float CheckboxPadding = 10;

        void OnDraw() override {
            const float thickness = Thickness * GetScale();

            {
                // label
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position.x = ScreenPosition.x + (CheckboxSize + CheckboxPadding) * GetScale();
                dti.Position.y = ScreenPosition.y + ScreenSize.y * 0.5f - _textSize.y * 0.5f * GetScale();
                Render::UICanvas->DrawRaw(_label, dti, Layer + 1);
            }

            Render::HudCanvasPayload payload;
            payload.Texture = Render::Materials->White().Handle();
            payload.Layer = Layer;
            payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = Focused || Hovered ? ACCENT_GLOW : IDLE_BUTTON;

            const float yOffset = (ScreenSize.y - _textSize.y * GetScale()) * 0.5f;

            {
                float size = CheckboxSize * GetScale();
                auto position = ScreenPosition;
                position.y += yOffset;
                //position.y += ScreenSize.y * 0.5f - size * 0.5f; // center the checkbox

                // left edge
                payload.V0.Position = position;
                payload.V1.Position = Vector2(position.x + thickness, position.y + thickness);
                payload.V2.Position = Vector2(position.x + thickness, position.y + size - thickness);
                payload.V3.Position = Vector2(position.x, position.y + size);
                payload.RoundLayout();
                Render::UICanvas->Draw(payload);

                // top edge
                payload.V0.Position = position;
                payload.V1.Position = Vector2(position.x + thickness, position.y + thickness);
                payload.V2.Position = Vector2(position.x + size - thickness, position.y + thickness);
                payload.V3.Position = Vector2(position.x + size, position.y);
                payload.RoundLayout();
                Render::UICanvas->Draw(payload);

                // right edge
                payload.V0.Position = Vector2(position.x + size, position.y + size);
                payload.V1.Position = Vector2(position.x + size - thickness, position.y + size - thickness);
                payload.V2.Position = Vector2(position.x + size - thickness, position.y + thickness);
                payload.V3.Position = Vector2(position.x + size, position.y);
                payload.RoundLayout();
                Render::UICanvas->Draw(payload);

                // bottom edge
                payload.V0.Position = Vector2(position.x + size, position.y + size);
                payload.V1.Position = Vector2(position.x + size - thickness, position.y + size - thickness);
                payload.V2.Position = Vector2(position.x + thickness, position.y + size - thickness);
                payload.V3.Position = Vector2(position.x, position.y + size);
                payload.RoundLayout();
                Render::UICanvas->Draw(payload);
            }

            if (*_value) {
                float inset = thickness * 1.5f;
                float size = CheckboxSize * GetScale() - inset * 2;
                auto position = ScreenPosition + Vector2(inset, inset);
                //position.y += ScreenSize.y * 0.5f - size - inset; // center the checkbox
                position.y += yOffset; // center the checkbox

                // tl to br
                payload.V0.Position = position;
                payload.V1.Position = Vector2(position.x + thickness, position.y);
                payload.V2.Position = Vector2(position.x + size, position.y + size - thickness);
                payload.V3.Position = Vector2(position.x + size, position.y + size);
                payload.RoundLayout();
                Render::UICanvas->Draw(payload);

                payload.V0.Position = position;
                payload.V1.Position = Vector2(position.x, position.y + thickness);
                payload.V2.Position = Vector2(position.x + size - thickness, position.y + size);
                payload.V3.Position = Vector2(position.x + size, position.y + size);
                payload.RoundLayout();
                Render::UICanvas->Draw(payload);

                // tr to bl
                payload.V0.Position = Vector2(position.x + size, position.y);
                payload.V1.Position = Vector2(position.x + size - thickness, position.y);
                payload.V2.Position = Vector2(position.x, position.y + size - thickness);
                payload.V3.Position = Vector2(position.x, position.y + size);
                payload.RoundLayout();
                Render::UICanvas->Draw(payload);

                payload.V0.Position = Vector2(position.x + size, position.y);
                payload.V1.Position = Vector2(position.x + size, position.y + thickness);
                payload.V2.Position = Vector2(position.x + thickness, position.y + size);
                payload.V3.Position = Vector2(position.x, position.y + size);
                payload.RoundLayout();
                Render::UICanvas->Draw(payload);
            }
        }

    protected:
        bool HandleOnClick() override {
            *_value = !*_value;
            return true;
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

        bool HandleMenuAction(Input::MenuActionState action) override {
            int increment = 0;
            int mult = Input::ShiftDown ? 10 : 1;

            if (action == MenuAction::Left)
                increment = -1;

            if (action == MenuAction::Right)
                increment = 1;

            if (increment != 0) {
                SetValue(*_value + increment * mult);
                return true;
            }

            return false;
        }

        void OnUpdate() override {
            if (!Focused) return;

            int increment = 0;
            int mult = Input::ShiftDown ? 10 : 1;

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
                dti.Position = ScreenPosition + Padding * scale;
                auto textLen = MeasureString(_text, FontSize::Medium).x;
                dti.Position.x += ScreenSize.x / 2 - textLen / 2 - Padding.x * scale; // center justify text
                dti.Position.y += 1 * scale; // offset from top slightly
                //dti.Position.x += ScreenSize.x / scale - textLen - Padding.x - Margin.x - size * 1.75f / scale; // right justify text
                //dti.HorizontalAlign = AlignH::Center;
                Render::UICanvas->DrawRaw(_text, dti, Layer + 1);
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
            Size = Vector2(60, CONTROL_HEIGHT);
            LabelWidth = textSize.x + _barPadding;
            UpdateValueText();
        }

        void UpdateValueText() {
            _valueText = std::to_string(*_value);
        }

        int Min, Max;
        float LabelWidth = 0;
        float ValueWidth = 25;
        string ChangeSound; // MENU_SELECT_SOUND
        bool ShowValue = false;

        std::function<void(int)> OnChange;

        void UpdatePercent(float percent) {
            auto value = (int)std::floor((Max - Min) * percent) + Min;
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
                auto barWidth = (Size.x - LabelWidth - GetValueWidth() - _barPadding) * GetScale();
                auto barPosition = ScreenPosition.x + LabelWidth * GetScale();
                auto tickWidth = barWidth / (Max - Min);

                auto percent = Saturate((Input::MousePosition.x - barPosition + tickWidth / 2) / barWidth);
                UpdatePercent(percent);
            }
        }

        bool CheckHover() {
            auto barWidth = (Size.x - LabelWidth - GetValueWidth() - _barPadding) * GetScale();
            auto barPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);
            return RectangleContains(barPosition, { barWidth, ScreenSize.y }, Input::MousePosition);
        }

        void DrawLeftBar(const Vector2& position, float width, float percent, bool hovered) const {
            const float barHeight = 6 * GetScale();

            Render::CanvasBitmapInfo cbi;
            cbi.Position = position;
            cbi.Position.y += ScreenSize.y / 2 - barHeight / 2;
            cbi.Size = Vector2(width * percent, barHeight);
            cbi.Texture = Render::Materials->White().Handle();
            //auto color = hovered ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
            auto color = hovered ? ACCENT_GLOW : Focused ? FOCUSED_BUTTON : IDLE_BUTTON;
            cbi.Color = color * 0.8f;
            Render::UICanvas->DrawBitmap(cbi, Layer + 1);
        }

        void DrawRightBar(const Vector2& position, float width, float percent, bool hovered) const {
            // Bar (right half)
            const float barHeight = 2 * GetScale();
            Render::CanvasBitmapInfo cbi;
            cbi.Position = position;
            cbi.Position.y += ScreenSize.y / 2 - barHeight / 2;
            cbi.Position.x += width * percent;
            cbi.Size = Vector2(width * (1 - percent), barHeight);
            cbi.Texture = Render::Materials->White().Handle();
            cbi.Color = hovered ? FOCUS_COLOR : Focused ? HOVER_COLOR : IDLE_BUTTON;
            cbi.Color *= 0.75f;
            Render::UICanvas->DrawBitmap(cbi, Layer + 1);
        }

        void DrawNotch(Vector2 position, float width, float percent, bool hovered) const {
            auto color = hovered ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
            const float notchHeight = 20 * GetScale();
            float notchWidth = 8 * GetScale();

            position.x += (width - notchWidth) * percent;
            position.y += ScreenSize.y / 2 - notchHeight / 2;

            Render::HudCanvasPayload payload;
            payload.Texture = Render::Materials->White().Handle();
            payload.Layer = Layer + 1;
            payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = color;

            payload.V0.Position = position;
            payload.V1.Position = Vector2(position.x + notchWidth, position.y + notchWidth);
            payload.V2.Position = Vector2(position.x + notchWidth, position.y + notchHeight);
            payload.V3.Position = Vector2(position.x, position.y + notchHeight);
            Render::UICanvas->Draw(payload);
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
            auto barWidth = (Size.x - LabelWidth - GetValueWidth() - _barPadding) * GetScale();
            auto barPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y - 3 * GetScale());
            auto percent = GetPercent();

            DrawLeftBar(barPosition, barWidth, percent, hovered);
            DrawRightBar(barPosition, barWidth, percent, hovered);
            DrawNotch(barPosition, barWidth, percent, hovered);

            {
                // Label
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position = ScreenPosition;
                Render::UICanvas->DrawRaw(_label, dti, Layer + 1);
            }

            if (ShowValue) {
                // Value
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position = Vector2(ScreenPosition.x + ScreenSize.x - ValueWidth * GetScale(), ScreenPosition.y);
                Render::UICanvas->DrawRaw(_valueText, dti, Layer + 1);
            }
        }

    private:
        float GetPercent() const {
            return float(*_value - Min) / float(Max - Min);
        }
    };

    class SliderSelect : public ControlBase {
        string _label;
        List<string> _values;
        gsl::strict_not_null<int*> _index;
        bool _held = false;
        string _valueText;
        float _barPadding = 10;
        bool _dragging = false;

    public:
        SliderSelect(string_view label, std::initializer_list<string_view> values, int& index)
            : _label(label), _values({ values.begin(), values.end() }), _index(&index) {
            Vector2 size;

            for (auto value : values) {
                auto textSize = MeasureString(value, FontSize::Medium);
                size = Vector2(std::max(size.x, textSize.x), std::max(size.y, textSize.y));
            }

            Size = Vector2(60, CONTROL_HEIGHT);
            ValueWidth = size.x + _barPadding + 10;
            LabelWidth = MeasureString(label, FontSize::Medium).x;
        }


        static Ptr<SliderSelect> Create(string_view label, std::initializer_list<string_view> values, int& index) {
            return make_unique<SliderSelect>(label, values, index);
        }

        float LabelWidth = 0;
        float ValueWidth = 25;
        string ChangeSound; // MENU_SELECT_SOUND
        bool ShowValue = false;

        std::function<void(int)> OnChange;

        bool HandleMenuAction(Input::MenuActionState action) override {
            int value = 0;

            if (action == MenuAction::Left)
                value = -1;

            if (action == MenuAction::Right)
                value = 1;

            if (value != 0) {
                *_index += value;
                *_index = std::clamp(*_index, 0, (int)_values.size() - 1);
                return true;
            }

            return false;
        }

        float GetBarWidth() const {
            return (Size.x - LabelWidth - ValueWidth - _barPadding) * GetScale();
        }

        float GetTickWidth() const {
            return GetBarWidth() / _values.size();
        }

        bool CheckHover() {
            auto barWidth = (Size.x - LabelWidth - ValueWidth - _barPadding) * GetScale();
            auto barPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);
            return RectangleContains(barPosition, { barWidth, ScreenSize.y }, Input::MousePosition);
        }

        void OnDraw() override {
            auto hovered = CheckHover();
            auto selectedColor = hovered ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
            auto color = hovered ? FOCUS_COLOR : Focused ? HOVER_COLOR : IDLE_BUTTON;
            color *= 0.6f;

            const float notchHeight = 10 * GetScale();
            float notchPadding = 10 * GetScale();

            auto position = Vector2(ScreenPosition.x + LabelWidth * GetScale() + ValueWidth * GetScale(), ScreenPosition.y - 3 * GetScale());
            //auto barWidth = GetBarWidth();
            auto tickWidth = GetTickWidth();
            //position.x += (barWidth - notchWidth);
            position.y += ScreenSize.y / 2 - notchHeight / 2;

            for (size_t i = 0; i < _values.size(); i++) {
                Render::HudCanvasPayload payload;
                payload.Texture = Render::Materials->White().Handle();
                payload.Layer = Layer + 1;
                payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = i == *_index ? selectedColor : color;

                payload.V0.Position = Vector2(position.x - notchHeight / 2, position.y); // tl
                payload.V1.Position = Vector2(position.x + notchHeight / 2, position.y + notchHeight); // bl
                payload.V2.Position = Vector2(position.x + tickWidth + notchHeight / 2 - notchPadding, position.y + notchHeight); // br
                payload.V3.Position = Vector2(position.x + tickWidth - notchHeight / 2 - notchPadding, position.y); // tr
                Render::UICanvas->Draw(payload);

                position.x += tickWidth;
            }

            {
                // Label
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position = ScreenPosition;
                Render::UICanvas->DrawRaw(_label, dti, Layer + 1);
            }

            if (auto value = Seq::tryItem(_values, *_index)) {
                // Value
                auto valueSize = MeasureString(*value, FontSize::Medium).x * GetScale();

                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                //dti.Position = Vector2(ScreenPosition.x + ScreenSize.x - valueSize.x * GetScale(), ScreenPosition.y) / GetScale();
                dti.Position = Vector2(ScreenPosition.x + LabelWidth * GetScale() + ValueWidth * GetScale() * 0.5f - valueSize * 0.5f, ScreenPosition.y);
                Render::UICanvas->DrawRaw(*value, dti, Layer + 1);
            }
        }
    };

    // Horizontal spinner that toggles through multiple options
    class OptionSpinner : public ControlBase {
        string _label;
        List<string> _values;
        gsl::strict_not_null<int*> _index;
        string _valueText;
        float _barPadding = 10;
        bool _dragging = false;
        float _arrowHeight = 18;
        float _arrowThickness = 8;
        float _textHeight = 24;

    public:
        OptionSpinner(string_view label, std::initializer_list<string_view> values, int& index)
            : _label(label), _values({ values.begin(), values.end() }), _index(&index) {
            auto size = MeasureString(label, FontSize::Medium);
            _textHeight = size.y;
            Size = Vector2(300, CONTROL_HEIGHT);
            ValueWidth = size.x + _barPadding + 10;
            LabelWidth = MeasureString(label, FontSize::Medium).x;
        }

        static Ptr<OptionSpinner> Create(string_view label, std::initializer_list<string_view> values, int& index) {
            return make_unique<OptionSpinner>(label, values, index);
        }

        float LabelWidth = 0;
        float ValueWidth = 25;
        string ChangeSound = MENU_SELECT_SOUND;
        bool ShowValue = false;

        std::function<void(int)> OnChange;

        bool HandleMenuAction(Input::MenuActionState action) override {
            auto value = 0;

            if (action == MenuAction::Left)
                value = -1;

            if (action == MenuAction::Right)
                value = 1;

            if (value != 0) {
                SetIndex(*_index + value);
                return true;
            }

            return false;
        }

        void OnUpdate() override {
            ControlBase::OnUpdate();

            if (!Focused) return;

            auto index = *_index;

            if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick)) {
                if (CheckArrowHover(GetLeftArrowPosition()))
                    index--;
                else if (CheckArrowHover(GetRightArrowPosition()))
                    index++;
            }

            SetIndex(index);
        }

        void SetIndex(int index) const {
            // Wrap
            if (index < 0) index = (int)_values.size() - 1;
            if (index >= _values.size()) index = 0;

            if (index != *_index) {
                Sound::Play2D(ChangeSound);
                *_index = index;
                if (OnChange) OnChange(index);
            }
        }

        bool CheckArrowHover(Vector2 position) const {
            position.x -= _arrowHeight * GetScale() / 4; // Center the hitbox on the arrow, as they are taller than wide
            return RectangleContains(position, { _arrowHeight * GetScale() * 1.25f, ScreenSize.y }, Input::MousePosition);
        }

        void DrawLeftArrow(const Vector2& position, float thickness, float height, const Color& color) const {
            Render::HudCanvasPayload payload;
            payload.Texture = Render::Materials->White().Handle();
            payload.Layer = Layer + 1;
            payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = color;

            auto width = height / 2;

            payload.V0.Position = Vector2(position.x + width, position.y); // tr
            payload.V1.Position = Vector2(position.x + width + thickness, position.y); // tr2
            payload.V2.Position = Vector2(position.x + thickness, position.y + width); // mid
            payload.V3.Position = Vector2(position.x, position.y + width); // mid
            Render::UICanvas->Draw(payload);

            payload.V0.Position = Vector2(position.x + width, position.y + height); // br
            payload.V1.Position = Vector2(position.x + width + thickness, position.y + height); // br2
            payload.V2.Position = Vector2(position.x + thickness, position.y + width); // mid
            payload.V3.Position = Vector2(position.x, position.y + width); // mid
            Render::UICanvas->Draw(payload);
        }

        void DrawRightArrow(const Vector2& position, float thickness, float height, const Color& color) const {
            Render::HudCanvasPayload payload;
            payload.Texture = Render::Materials->White().Handle();
            payload.Layer = Layer + 1;
            payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = color;
            auto width = height / 2;

            payload.V0.Position = Vector2(position.x, position.y); // tr
            payload.V1.Position = Vector2(position.x + thickness, position.y); // tr2
            payload.V2.Position = Vector2(position.x + width + thickness, position.y + width); // mid
            payload.V3.Position = Vector2(position.x + width, position.y + width); // mid
            Render::UICanvas->Draw(payload);

            payload.V0.Position = Vector2(position.x, position.y + height); // br
            payload.V1.Position = Vector2(position.x + thickness, position.y + height); // br2
            payload.V2.Position = Vector2(position.x + width + thickness, position.y + width); // mid
            payload.V3.Position = Vector2(position.x + width, position.y + width); // mid
            Render::UICanvas->Draw(payload);
        }

        Vector2 GetLeftArrowPosition() const {
            auto labelWidth = LabelWidth * GetScale();
            auto position = Vector2(ScreenPosition.x + labelWidth, ScreenPosition.y + 1 * GetScale());
            position.x += 10 * GetScale();
            return position;
        }

        Vector2 GetRightArrowPosition() const {
            return { ScreenPosition.x + ScreenSize.x - _arrowHeight * GetScale() / 2, ScreenPosition.y + 1 * GetScale() };
        }

        void OnDraw() override {
            //auto hovered = CheckHover();
            //auto selectedColor = hovered ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
            //auto color = hovered ? FOCUS_COLOR : Focused ? HOVER_COLOR : IDLE_BUTTON;
            auto labelWidth = LabelWidth * GetScale();

            if (Focused) {
                float arrowHeight = _arrowHeight * GetScale();
                float arrowThickness = _arrowThickness * GetScale();

                {
                    auto leftPos = GetLeftArrowPosition();
                    auto leftHover = CheckArrowHover(leftPos);
                    auto selectedColor = leftHover ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
                    DrawLeftArrow(leftPos, arrowThickness, arrowHeight, selectedColor);
                }

                {
                    auto rightPos = GetRightArrowPosition();
                    auto rightHover = CheckArrowHover(rightPos);
                    auto selectedColor = rightHover ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
                    DrawRightArrow(rightPos, arrowThickness, arrowHeight, selectedColor);
                }
            }

            const float yOffset = (ScreenSize.y - _textHeight * GetScale()) * 0.5f;

            {
                // Label
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position.x = ScreenPosition.x;
                dti.Position.y = ScreenPosition.y + yOffset;
                Render::UICanvas->DrawRaw(_label, dti, Layer + 1);
            }

            if (auto value = Seq::tryItem(_values, *_index)) {
                // Value
                auto valueSize = MeasureString(*value, FontSize::Medium).x * GetScale();

                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                //dti.Position = Vector2(ScreenPosition.x + ScreenSize.x - valueSize.x * GetScale(), ScreenPosition.y) / GetScale();
                float valueWidth = ScreenSize.x - labelWidth;
                dti.Position.x = ScreenPosition.x + labelWidth + valueWidth / 2 - valueSize / 2;
                dti.Position.y = ScreenPosition.y + yOffset;
                Render::UICanvas->DrawRaw(*value, dti, Layer + 1);
            }
        }
    };

    class SliderFloat : public ControlBase {
        string _label;
        gsl::strict_not_null<float*> _value;
        bool _held = false;
        string _valueText;
        float _barPadding = 10;
        bool _dragging = false;
        float _min, _max;
        uint _precision;

    public:
        SliderFloat(string_view label, float min, float max, float& value, uint precision = 3)
            : _label(label), _value(&value), _min(min), _max(max), _precision(precision) {
            auto textSize = MeasureString(_label, FontSize::Medium);
            Size = Vector2(60, CONTROL_HEIGHT);
            LabelWidth = textSize.x + _barPadding;
            UpdateValueText();
        }

        void UpdateValueText() {
            _valueText = fmt::format("{:.{}f}", *_value, _precision);
        }

        float LabelWidth = 0;
        float ValueWidth = 25;
        SoundResource ChangeSound; // MENU_SELECT_SOUND
        bool ShowValue = false;

        std::function<void(float)> OnChange;

        void UpdatePercent(float percent, bool playSound = true) {
            percent = Saturate(percent);
            auto value = (_max - _min) * percent + _min;

            if (*_value != value) {
                *_value = value;
                if (OnChange) OnChange(value);
                UpdateValueText();

                if (playSound)
                    Sound::Play2D(ChangeSound);
            }
        }

        float GetValueWidth() const { return ShowValue ? ValueWidth : 0; }

        bool HandleMenuAction(Input::MenuActionState action) override {
            const float keyboardIncrement = Input::ShiftDown ? 0.01f : 0.1f;

            if (action == MenuAction::Left) {
                UpdatePercent(GetPercent() - keyboardIncrement);
                return true;
            }

            if (action == MenuAction::Right) {
                UpdatePercent(GetPercent() + keyboardIncrement);
                return true;
            }

            return false;
        }

        void OnUpdate() override {
            if (!Focused) return;

            // when clicked, move cursor to nearest increment, and then continue to update as dragged
            if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick) && CheckHover()) {
                _dragging = true;
                CaptureCursor(true);
            }
            else if (!Input::IsMouseButtonDown(Input::MouseButtons::LeftClick)) {
                if (_dragging) {
                    _dragging = false;
                    CaptureCursor(false);
                    Sound::Play2D(ChangeSound);
                }
            }

            if (_dragging) {
                auto barWidth = (Size.x - LabelWidth - GetValueWidth() - _barPadding) * GetScale();
                auto barPosition = ScreenPosition.x + LabelWidth * GetScale();
                auto percent = Saturate((Input::MousePosition.x - barPosition) / barWidth);
                UpdatePercent(percent, false);
            }
        }

        bool CheckHover() {
            if (!Focused) return false;
            auto barWidth = (Size.x - LabelWidth - GetValueWidth() - _barPadding) * GetScale();
            auto barPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);
            return RectangleContains(barPosition, { barWidth, ScreenSize.y }, Input::MousePosition);
        }

        void DrawLeftBar(const Vector2& position, float width, float percent, bool hovered) const {
            const float barHeight = 6 * GetScale();

            Render::CanvasBitmapInfo cbi;
            cbi.Position = position;
            cbi.Position.y += ScreenSize.y / 2 - barHeight / 2;
            cbi.Size = Vector2(width * percent, barHeight);
            cbi.Texture = Render::Materials->White().Handle();
            //auto color = hovered ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
            auto color = hovered ? ACCENT_GLOW : Focused ? FOCUSED_BUTTON : IDLE_BUTTON;
            cbi.Color = color * 0.8f;
            Render::UICanvas->DrawBitmap(cbi, Layer + 1);
        }

        void DrawRightBar(const Vector2& position, float width, float percent, bool hovered) const {
            // Bar (right half)
            const float barHeight = 2 * GetScale();
            Render::CanvasBitmapInfo cbi;
            cbi.Position = position;
            cbi.Position.y += ScreenSize.y / 2 - barHeight / 2;
            cbi.Position.x += width * percent;
            cbi.Size = Vector2(width * (1 - percent), barHeight);
            cbi.Texture = Render::Materials->White().Handle();
            cbi.Color = hovered ? FOCUS_COLOR : Focused ? HOVER_COLOR : IDLE_BUTTON;
            cbi.Color *= 0.75f;
            Render::UICanvas->DrawBitmap(cbi, Layer + 1);
        }

        void DrawNotch(Vector2 position, float width, float percent, bool hovered) const {
            auto color = hovered ? ACCENT_GLOW : Focused ? ACCENT_COLOR : IDLE_BUTTON;
            const float notchHeight = 20 * GetScale();
            float notchWidth = 8 * GetScale();

            position.x += (width - notchWidth) * percent;
            position.y += ScreenSize.y / 2 - notchHeight / 2;

            Render::HudCanvasPayload payload;
            payload.Texture = Render::Materials->White().Handle();
            payload.Layer = Layer + 1;
            payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = color;

            payload.V0.Position = position;
            payload.V1.Position = Vector2(position.x + notchWidth, position.y + notchWidth);
            payload.V2.Position = Vector2(position.x + notchWidth, position.y + notchHeight);
            payload.V3.Position = Vector2(position.x, position.y + notchHeight);
            Render::UICanvas->Draw(payload);
        }

        void OnDraw() override {
            const auto scale = GetScale();
            auto hovered = _dragging || CheckHover();
            auto barWidth = (Size.x - LabelWidth - GetValueWidth() - _barPadding) * scale;
            auto barPosition = Vector2(ScreenPosition.x + LabelWidth * scale, ScreenPosition.y - 3 * scale);
            auto percent = GetPercent();

            DrawLeftBar(barPosition, barWidth, percent, hovered);
            DrawRightBar(barPosition, barWidth, percent, hovered);
            DrawNotch(barPosition, barWidth, percent, hovered);

            {
                // Label
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position = ScreenPosition;
                dti.Position.y += CONTROL_PADDING * scale;
                Render::UICanvas->DrawRaw(_label, dti, Layer + 1);
            }

            if (ShowValue) {
                // Value
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position = Vector2(ScreenPosition.x + ScreenSize.x - ValueWidth * scale, ScreenPosition.y + CONTROL_PADDING * scale);
                Render::UICanvas->DrawRaw(_valueText, dti, Layer + 1);
            }
        }

    private:
        float GetPercent() const {
            return (*_value - _min) / (_max - _min);
        }
    };

    bool CloseScreen();

    enum class CloseState { None, Accept, Cancel };

    class ScreenBase : public ControlBase {
    public:
        ScreenBase() {
            Selectable = false;
            Padding = Vector2(5, 5);
            ActionSound = MENU_SELECT_SOUND;
        }

        bool CloseOnConfirm = true;
        CloseState State = CloseState::None;

        ControlBase* Selection = nullptr;
        ControlBase* LastGoodSelection = nullptr;
        std::function<void(CloseState)> CloseCallback;

        void OnUpdate() override {
            if (Input::MouseMoved() && !IsCursorCaptured()) {
                // Update selection when cursor moves, but only if the control is valid
                if (auto control = HitTestCursor())
                    SetSelection(control);
            }

            ControlBase::OnUpdate();
        }

        // Called when the screen is first shown
        virtual void OnShow() {}

        // Called when a top level screen tries to close. Return true if it should close.
        virtual bool OnTryClose() { return false; }

        // Called when a screen is closed.
        virtual void OnClose() {}

        bool OnConfirm() override {
            if (Selection) {
                return Selection->OnConfirm();
            }
            else if (CloseOnConfirm) {
                // Play the default menu select sound when closing if there's no action
                Sound::Play2D(SoundResource{ MENU_SELECT_SOUND });
            }

            if (CloseOnConfirm) {
                State = CloseState::Accept;
                return true;
            }

            return false;
        }

        void OnUpdateLayout() override {
            // Fill the whole screen if the size is zero
            auto& canvasSize = Render::UICanvas->GetSize();
            ScreenSize = Size == Vector2::Zero ? canvasSize : Size * GetScale();
            ScreenPosition = Render::GetAlignment(ScreenSize, HorizontalAlignment, VerticalAlignment, canvasSize)
                             + Position * GetScale();
            ControlBase::OnUpdateLayout();
        }

        // Moves the selection to a new control
        void SetSelection(ControlBase* control) {
            //if (control) {
            //    if (!control->Visible)
            //        return; // Don't select invisible controls
            //}

            if (Selection) Selection->Focused = false;
            Selection = control;

            if (control /*&& control->Visible*/) {
                control->Focused = true;
                control->OnSelect();

                if (control->Parent)
                    control->Parent->OnChildSelected(control);

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

        bool HandleMenuAction(Input::MenuActionState action) override {
            // Allow the selected control to handle input first
            if (Selection && Selection->HandleMenuAction(action))
                return true;

            if (action == MenuAction::Confirm) {
                return OnConfirm();
            }

            if (action == MenuAction::Cancel) {
                State = CloseState::Cancel;
                return true;
            }

            if (action == MenuAction::Down) {
                OnDownArrow();
                return true;
            }

            if (action == MenuAction::Up) {
                OnUpArrow();
                return true;
            }

            return false;
        }

    protected:
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

            if (index == tree.size() - 1 || index == -1) {
                SetSelection(tree.front()); // wrap
            }
            else {
                SetSelection(tree[index + 1]);
            }
        }
    };

    class DialogBase : public ScreenBase {
    protected:
        string _title;
        Vector2 _titleSize;

    public:
        Color BorderColor = BORDER_COLOR;

        DialogBase(string_view title = "", bool showCloseButton = true): _title(title) {
            HorizontalAlignment = AlignH::Center;
            VerticalAlignment = AlignV::Center;
            _titleSize = MeasureString(title, FontSize::Medium);

            if (showCloseButton) {
                auto close = make_unique<CloseButton>([this] { OnDialogClose(); });
                close->HorizontalAlignment = AlignH::Right;
                close->Margin = Vector2(DIALOG_PADDING, DIALOG_PADDING);
                AddChild(std::move(close));
            }

            //if (!title.empty()) {
            //    auto titleLabel = make_unique<Label>(title, FontSize::MediumBlue);
            //    titleLabel->VerticalAlignment = AlignV::Top;
            //    titleLabel->HorizontalAlignment = AlignH::Center;
            //    titleLabel->Position = Vector2(0, DIALOG_PADDING);
            //    titleLabel->Color = DIALOG_TITLE_COLOR;
            //    AddChild(std::move(titleLabel));
            //}
        }

        AlignH TitleAlignment = AlignH::Center;
        bool CloseOnClickOutside = true; // Clicking outside of the dialog closes it with a cancel status

        void OnUpdate() override {
            ScreenBase::OnUpdate();

            if (IsInputCaptured() || IsCursorCaptured())
                return;

            if (CloseOnClickOutside && Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick) && !RectangleContains(ScreenPosition, ScreenSize, Input::MousePosition)) {
                State = CloseState::Cancel;
                //Sound::Play2D(SoundResource{ MENU_BACK_SOUND });
            }
        }

        virtual void OnDialogClose() {
            State = CloseState::Cancel;
        }

        void OnDraw() override {
            const auto border = Vector2(1, 1) * GetScale();

            if (!_title.empty()) {
                // Label
                Render::DrawTextInfo dti;
                dti.Font = FontSize::MediumBlue;
                dti.Color = DIALOG_TITLE_COLOR;
                dti.Position = ScreenPosition + Render::GetAlignment(_titleSize * GetScale(), TitleAlignment, AlignV::Top, ScreenSize);
                dti.Position.y += DIALOG_PADDING * GetScale();
                if (TitleAlignment == AlignH::Left) dti.Position.x += DIALOG_PADDING * GetScale();
                Render::UICanvas->DrawRaw(_title, dti, Layer + 1);
            }

            {
                // Border
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition;
                cbi.Size = ScreenSize;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = BorderColor;
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


    // Centered selection popup using a medium font size
    class MediumSelectionPopup : public DialogBase {
        gsl::strict_not_null<int*> _index;

    public:
        MediumSelectionPopup(const List<string>& values, int& index) : DialogBase("", false), _index(&index) {
            auto panel = make_unique<StackPanel>();
            panel->Position = Vector2{ DIALOG_PADDING, DIALOG_PADDING };
            CloseOnClickOutside = true;

            float width = 250;
            float maxWidth = 630;

            for (int i = 0; i < values.size(); i++) {
                auto value = TrimStringByLength(values[i], FontSize::Medium, (int)maxWidth);
                auto labelSize = MeasureString(value, FontSize::Medium);
                width = std::max(labelSize.x, width);

                auto button = panel->AddChild<Button>(value, [this, i] {
                    *_index = i;
                    State = CloseState::Accept;
                });

                button->ActionSound = ""; // No sound, closing dialog plays one
            }

            Size = Vector2(std::min(width + DIALOG_PADDING * 2, maxWidth), CONTROL_HEIGHT * values.size() + DIALOG_PADDING * 2);
            AddChild(std::move(panel));
        }
    };

    // Selection popup aligned with the dropdown and uses a small font
    class SelectionPopup : public DialogBase {
        gsl::strict_not_null<int*> _index;
        gsl::strict_not_null<ControlBase*> _parent;
        Vector2 _offset;
        ControlBase* _initialSelection = nullptr;

    public:
        float Padding = 4;

        SelectionPopup(const List<string>& values, int& index, ControlBase& parent, const Vector2& offset = Vector2::Zero, float width = 0) :
            DialogBase("", false), _index(&index), _parent(&parent), _offset(offset) {
            auto stack = AddChild<StackPanel>();
            stack->Position = Vector2{ Padding, Padding };

            float maxWidth = 630 - parent.Position.x;
            //float width = 0;

            for (int i = 0; i < values.size(); i++) {
                auto value = TrimStringByLength(values[i], FontSize::Small, (int)maxWidth);
                auto labelSize = MeasureString(value, FontSize::Small);
                width = std::max(labelSize.x, width);

                auto button = stack->AddChild<Button>(value, [this, i] {
                    *_index = i;
                    State = CloseState::Accept;
                }, AlignH::Center, FontSize::Small);

                button->TextColor = HOVER_COLOR;
                button->ActionSound = ""; // No sound, closing dialog plays one
                if (i == *_index) _initialSelection = button;
            }


            Size = Vector2(std::min(width, maxWidth), SMALL_CONTROL_HEIGHT * values.size() + Padding * 3);
            stack->Size.x = Size.x - Padding * 2;

            CloseOnClickOutside = true;
            Position = parent.ScreenPosition;
            HorizontalAlignment = AlignH::Left;
            VerticalAlignment = AlignV::Top;
            BorderColor = ACCENT_GLOW;

            //SetValues(values);
        }

        //void SetValues(const List<string>& values) {
        //    Children.clear();
        //    float maxWidth = 630 - _parent->Position.x;
        //    float width = _width;
        //    auto stack = AddChild<StackPanel>();
        //    stack->Position = Vector2{ Padding, Padding };

        //    for (int i = 0; i < values.size(); i++) {
        //        auto value = TrimStringByLength(values[i], FontSize::Small, (int)maxWidth);
        //        auto labelSize = MeasureString(value, FontSize::Small);
        //        width = std::max(labelSize.x, width);

        //        auto button = stack->AddChild<Button>(value, [this, i] {
        //            *_index = i;
        //            State = CloseState::Accept;
        //        }, AlignH::Center, FontSize::Small);

        //        button->TextColor = HOVER_COLOR;
        //        button->ActionSound = ""; // No sound, closing dialog plays one
        //        if (i == *_index) _initialSelection = button;
        //    }

        //    Size = Vector2(std::min(width, maxWidth), SMALL_CONTROL_HEIGHT * values.size() + Padding * 3);
        //    stack->Size.x = Size.x - Padding * 2;
        //}

        void OnUpdateLayout() override {
            DialogBase::OnUpdateLayout();

            Position = _parent->ScreenPosition / GetScale() + _offset;
            Position.y += _parent->ScreenSize.y / GetScale();
        }

        void OnShow() override {
            SetSelection(_initialSelection);
        }
    };

    ScreenBase* ShowScreen(Ptr<ScreenBase> screen);

    // Shows a box with a label and value that can be activated to show a popup
    class ComboSelect : public ControlBase {
        string _label;
        List<string> _values;
        gsl::strict_not_null<int*> _index;
        bool _held = false;
        string _valueText;
        bool _dragging = false;
        bool _hovered = false;
        float _textHeight = 24;

    public:
        float LabelWidth = 0;
        float ValueWidth = 25;
        string MenuActionSound = MENU_SELECT_SOUND; // Sound when picking an item in the popup menu

        std::function<void(int)> OnChange; // Called when a value is selected

        ComboSelect(string_view label, const List<string>& values, int& index)
            : _label(label), _index(&index) {
            //Vector2 size;

            //for (auto& value : values) {
            //    auto textSize = MeasureString(value, FontSize::Medium);
            //    size = Vector2(std::max(size.x, textSize.x), std::max(size.y, textSize.y));
            //}

            Size = Vector2(60, CONTROL_HEIGHT);
            //Padding = Vector2(0, 2);
            auto labelSize = MeasureString(label, FontSize::Medium);
            LabelWidth = labelSize.x;
            _textHeight = labelSize.y;
            //ValueWidth = size.x - LabelWidth;
            ActionSound = MENU_SELECT_SOUND;

            SetValues(values);
        }

        void SetValues(const List<string>& values) {
            _values = values;

            //Vector2 size;

            //for (auto& value : values) {
            //    auto textSize = MeasureString(value, FontSize::Medium);
            //    size = Vector2(std::max(size.x, textSize.x), std::max(size.y, textSize.y));
            //}

            //ValueWidth = size.x - LabelWidth;
        }

        static Ptr<ComboSelect> Create(string_view label, const List<string>& values, int& index) {
            return make_unique<ComboSelect>(label, values, index);
        }

        void OnUpdateLayout() override {
            ValueWidth = Size.x - LabelWidth;
        }

        bool HandleMenuAction(Input::MenuActionState action) override {
            if (action == MenuAction::Confirm) {
                SetSelection(this);
                ShowPopup();
                return true;
            }

            return false;
        }

        void ShowPopup() {
            if (OpenCallback) OpenCallback();

            auto screen = make_unique<SelectionPopup>(_values, *_index, *this, Vector2(LabelWidth, -9), ValueWidth);
            screen->ActionSound = MenuActionSound;

            screen->CloseCallback = [this](CloseState state) {
                if (state == CloseState::Accept) {
                    SetSelection(this);
                    if (OnChange) OnChange(*_index);
                }
            };

            screen->Layer = Layer + 2;

            Sound::Play2D(SoundResource{ ActionSound });
            ShowScreen(std::move(screen));
        }

        Action OpenCallback;

        void OnUpdate() override {
            auto boxPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);
            _hovered = RectangleContains(boxPosition, Vector2(ValueWidth * GetScale(), ScreenSize.y), Input::MousePosition);

            if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick) && _hovered) {
                ShowPopup();
            }
        }

        void OnDraw() override {
            {
                // Label
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = Focused /*|| Hovered*/ ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position = ScreenPosition;
                dti.Position.y += CONTROL_PADDING * GetScale();
                Render::UICanvas->DrawRaw(_label, dti, Layer + 1);
            }

            auto boxPosition = Vector2(ScreenPosition.x + LabelWidth * GetScale(), ScreenPosition.y);
            auto borderColor = _hovered ? ACCENT_GLOW : Focused ? FOCUSED_BUTTON : IDLE_BUTTON;
            auto boxSize = Vector2(ValueWidth * GetScale(), 18 * GetScale());

            {
                // Border
                Render::CanvasBitmapInfo cbi;
                cbi.Position = boxPosition;
                //cbi.Size = Vector2(ValueWidth * GetScale(), ScreenSize.y - CONTROL_PADDING * GetScale() * 3);
                cbi.Size = boxSize;
                //.x = ValueWidth * GetScale();
                //cbi.Size.y = ScreenSize.y;
                cbi.Texture = Render::Materials->White().Handle();
                //cbi.Color = Focused ? ACCENT_COLOR : BORDER_COLOR;
                cbi.Color = borderColor;
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Background
                Render::CanvasBitmapInfo cbi;
                const auto border = Vector2(1, 1) * GetScale();
                cbi.Position = boxPosition + border;
                //cbi.Size = Vector2(ValueWidth * GetScale(), ScreenSize.y) - border * 2;
                cbi.Size = boxSize - Vector2(border.x * 2, border.y * 2);
                //cbi.Size.x = ValueWidth * GetScale() - border.x * 2;
                //cbi.Size.y = ScreenSize.y - border.y * 2;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = borderColor * 0.1f;
                cbi.Color.A(1);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            if (auto value = Seq::tryItem(_values, *_index)) {
                // Value
                auto trimmed = TrimStringByLength(*value, FontSize::Small, (int)ValueWidth);
                auto valueSize = MeasureString(trimmed, FontSize::Small).x;

                Render::DrawTextInfo dti;
                //dti.Font = Focused ? FontSize::MediumGold : FontSize::Medium;
                dti.Font = FontSize::Small;
                dti.Color = Focused /*|| Hovered*/ ? ACCENT_COLOR : Color(0.8f, 0.8f, 0.8f);
                // ACCENT_COLOR
                //dti.Color = borderColor;
                //dti.Position = Vector2(ScreenPosition.x + LabelWidth + ValueWidth * 0.5f - valueSize * 0.5f, ScreenPosition.y);
                dti.Position.x = ScreenPosition.x + (LabelWidth + ValueWidth * 0.5f - valueSize * 0.5f) * GetScale();
                dti.Position.y = ScreenPosition.y + (CONTROL_PADDING + 3) * GetScale(); // Shifting text off-center is not ideal, but there's no room
                Render::UICanvas->DrawRaw(trimmed, dti, Layer + 1);
            }
        }
    };
}
