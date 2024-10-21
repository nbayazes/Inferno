#include "pch.h"
#include "Game.UI.h"
#include "Game.Text.h"
#include "Graphics/Render.h"
#include "gsl/pointers"
#include "Input.h"
#include "Types.h"
#include "Utility.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Version.h"

namespace Inferno::UI {
    using Action = std::function<void()>;
    using ClickHandler = std::function<void(const Vector2*)>;
    using Inferno::Input::Keys;

    namespace {
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
    }

    float GetScale() {
        return Render::HudCanvas->GetScale();
    }

    // Returns true if a rectangle at a position and size contain a point
    bool RectangleContains(const Vector2 origin, const Vector2& size, const Vector2& point) {
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
                if (Selectable)
                    Focused = Contains(Input::MousePosition);

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

    enum class CloseState { None, Accept, Cancel };

    class ScreenBase : public ControlBase {
    public:
        ScreenBase() {
            Selectable = false;
            Padding = Vector2(5, 5);
        }

        CloseState State = CloseState::None;

        ControlBase* Selection = nullptr;
        ControlBase* LastGoodSelection = nullptr;
        std::function<void(CloseState)> CloseCallback;

        void OnUpdate() override {
            if (Input::MouseMoved())
                SetSelection(HitTestCursor());

            ControlBase::OnUpdate(); // breaks main menu selection
        }

        void OnConfirm() {
            if (Selection && Selection->ClickAction) {
                Sound::Play2D(SoundResource{ ActionSound });
                Selection->ClickAction();
                State = CloseState::Accept;
            }
        }

        void OnUpdateLayout() override {
            // Fill the whole screen if the size is zero
            auto& canvasSize = Render::HudCanvas->GetSize();
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
        int FindSelectionIndex(span<ControlBase*> tree) const {
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

    // Horizontal slider
    //template <typename TValue>
    //class Slider : public ControlBase {
    //public:
    //    float Width;
    //    TValue Min, Max, Value;

    //    void Clicked(const Vector2& position) {
    //        if (!Contains(position)) return;

    //        // determine the location within the slider and set the value
    //    }
    //};

    class Rectangle : public ControlBase {
    public:
        Rectangle() {}

        Color Fill;

        void OnDraw() override {
            Render::CanvasBitmapInfo cbi;
            cbi.Position = ScreenPosition;
            cbi.Size = ScreenSize;
            cbi.Texture = Render::Materials->White().Handle();
            cbi.Color = Fill;
            Render::HudCanvas->DrawBitmap(cbi, Layer);
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
            Render::HudCanvas->DrawText(_text, dti, Layer);
        }
    };

    // Translates an input keycode to an ASCII character
    uchar TranslateSymbol(uchar keycode) {
        switch (keycode) {
            case Keys::OemSemicolon: return ';';
            case Keys::OemPlus: return '=';
            case Keys::OemComma: return ',';
            case Keys::OemMinus: return '-';
            case Keys::OemPeriod: return '.';
            case Keys::OemQuestion: return '/';
            case Keys::OemTilde: return '`';
            case Keys::OemOpenBrackets: return '[';
            case Keys::OemPipe: return '\\';
            case Keys::OemCloseBrackets: return ']';
            case Keys::OemQuotes: return '\'';
            case Keys::OemBackslash: return '/';
            default: return '\0';
        }
    };

    // Shifts a character to its uppercase form
    uchar ShiftSymbol(uchar symbol) {
        switch (symbol) {
            case ';': return ':';
            case '=': return '+';
            case ',': return '<';
            case '.': return '>';
            case '-': return '_';
            case '/': return '?';
            case '`': return '~';
            case '[': return '{';
            case '\\': return '|';
            case ']': return '}';
            case '\'': return '"';
            default: return symbol;
        }
    };

    // Lookup for numbers to uppercase
    constexpr auto NUMERIC_SHIFT_TABLE = std::to_array<uchar>({ ')', '!', '@', '#', '$', '%', '^', '&', '*', '(' });

    uchar ShiftNumber(uchar number) {
        number -= Keys::D0;
        if (!Seq::inRange(NUMERIC_SHIFT_TABLE, number)) return number;
        return NUMERIC_SHIFT_TABLE[number];
    }

    class TextBox : public ControlBase {
        string _text;
        FontSize _font;
        float _cursorTimer = 0;
        size_t _maxLength;

    public:
        bool NumericMode = false;
        bool EnableSymbols = false; // Enable non-numeric, non-alphabetical characters
        Color TextColor = Color(1, 1, 1);
        Color FocusColor = FOCUS_COLOR;

        TextBox(size_t maxLength = 100, FontSize font = FontSize::Medium): _font(font), _maxLength(maxLength) {}

        void SetText(string_view text) {
            _text = text;
            Padding = Vector2(4, 4);
        }

        void OnUpdate() override {
            if (!Focused) return;

            using Input::Keys;
            auto pressed = Input::GetPressedKeys();
            auto repeated = Input::GetRepeatedKeys();

            for (uchar i = 0; i < 255; i++) {
                if (pressed[i] || repeated[i]) {
                    bool isNumeric = i >= Keys::D0 && i <= Keys::D9;
                    bool isNumpad = i >= Keys::NumPad0 && i <= Keys::NumPad9;
                    bool isLetter = i >= Keys::A && i <= Keys::Z;
                    auto symbol = TranslateSymbol(i);
                    auto isSymbol = symbol != '\0';

                    if (i == Keys::Delete || i == Keys::Back /*|| i == Keys::Left*/) {
                        if (!_text.empty()) {
                            _text.pop_back();
                            break;
                        }
                    }

                    if (_text.size() >= _maxLength)
                        break;

                    if (isNumpad) {
                        constexpr uchar numpadOffset = Keys::NumPad0 - Keys::D0;
                        _text += uchar(i - numpadOffset);
                    }
                    if (isSymbol) {
                        if (Input::ShiftDown)
                            symbol = ShiftSymbol(symbol);

                        _text += symbol;
                    }
                    else if (isNumeric || isLetter || isSymbol || i == Keys::Space) {
                        if (isLetter && Input::ShiftDown) {
                            constexpr uchar shift = 'a' - 'A';
                            _text += uchar(i + shift);
                        }
                        else if (isNumeric && Input::ShiftDown) {
                            _text += ShiftNumber(i);
                        }
                        else {
                            _text += i;
                        }
                    }
                }
            }
        }

        void OnDraw() override {
            {
                // Border
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition;
                cbi.Size = ScreenSize;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Focused ? ACCENT_COLOR : BORDER_COLOR;
                Render::HudCanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Background
                Render::CanvasBitmapInfo cbi;
                //cbi.Position = ScreenPosition;
                //cbi.Size = ScreenSize;
                const auto border = Vector2(1, 1) * GetScale();
                cbi.Position = ScreenPosition + border;
                cbi.Size = ScreenSize - border * 2;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Color(0, 0, 0, 1);
                Render::HudCanvas->DrawBitmap(cbi, Layer);
            }

            {
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : _font;
                dti.Color = Focused /*|| Hovered*/ ? FocusColor : TextColor;
                dti.Position = ScreenPosition / GetScale() + Margin + Padding;
                dti.EnableTokenParsing = false;
                Render::HudCanvas->DrawText(_text, dti, Layer + 1);
            }

            if (!Focused) return;

            // Draw cursor
            _cursorTimer += Clock.GetFrameTimeSeconds();
            while (_cursorTimer > 1) _cursorTimer -= 1;

            if (_cursorTimer > 0.5f) {
                // NOTE: the plain Medium font appears to be missing kerning info
                // for consecutive forward slashes `///`
                auto offset = MeasureString(_text, FontSize::MediumGold);

                Render::DrawTextInfo dti;
                dti.Font = FontSize::MediumGold;
                dti.Color = FocusColor;
                dti.Position = ScreenPosition / GetScale() + Margin + Padding;
                dti.Position.x += offset.x;
                Render::HudCanvas->DrawText("_", dti, Layer + 1);
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
                Render::HudCanvas->DrawBitmap(cbi, Layer);
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
                Render::HudCanvas->DrawBitmap(cbi, Layer);
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
                Render::HudCanvas->Draw(payload);
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
                Render::HudCanvas->Draw(payload);

                // top to bottom
                payload.V0.Position = Vector2(position.x + half - thickness, position.y);
                payload.V1.Position = Vector2(position.x + half + thickness, position.y);
                payload.V2.Position = Vector2(position.x + half + thickness, position.y + size);
                payload.V3.Position = Vector2(position.x + half - thickness, position.y + size);
                Render::HudCanvas->Draw(payload);
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
                Render::HudCanvas->DrawText(_text, dti, Layer + 1);
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
            Render::HudCanvas->DrawText(_text, dti, Layer);
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
            Render::HudCanvas->Draw(payload);

            payload.V0.Position = position;
            payload.V1.Position = Vector2(position.x, position.y + thickness);
            payload.V2.Position = Vector2(position.x + size - thickness, position.y + size);
            payload.V3.Position = Vector2(position.x + size, position.y + size);
            Render::HudCanvas->Draw(payload);

            // tr to bl
            payload.V0.Position = Vector2(position.x + size, position.y);
            payload.V1.Position = Vector2(position.x + size - thickness, position.y);
            payload.V2.Position = Vector2(position.x, position.y + size - thickness);
            payload.V3.Position = Vector2(position.x, position.y + size);
            payload.Layer = Layer;
            Render::HudCanvas->Draw(payload);

            payload.V0.Position = Vector2(position.x + size, position.y);
            payload.V1.Position = Vector2(position.x + size, position.y + thickness);
            payload.V2.Position = Vector2(position.x + thickness, position.y + size);
            payload.V3.Position = Vector2(position.x, position.y + size);
            payload.Layer = Layer;
            Render::HudCanvas->Draw(payload);
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

            Rectangle rect;
            AddChild(make_unique<Rectangle>(std::move(rect)));
            Padding = Vector2(2, 2);
        }

        void OnUpdate() override {
            // todo: check focus?

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

            if (Input::IsKeyPressed(Keys::Enter) && ClickItemAction) {
                ClickItemAction(_index);
            }
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
                Render::HudCanvas->DrawBitmap(cbi, Layer);
            }

            for (int i = _scrollIndex, j = 0; i < Items.size() && i < _scrollIndex + VisibleItems; i++, j++) {
                auto& item = Items[i];

                Render::DrawTextInfo dti;
                dti.Font = _index == i ? FontSize::MediumGold : FontSize::Medium;
                dti.Color = _index == i ? FOCUS_COLOR : Color(1, 1, 1);
                dti.Position = ScreenPosition / GetScale() + Padding;
                dti.Position.y += (_fontHeight + ItemSpacing) * j + LINE_OFFSET;
                Render::HudCanvas->DrawText(item, dti, Layer);
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
                    Render::HudCanvas->DrawBitmap(cbi, Layer + 1);
                }
            }
        }
    };


    List<Ptr<ScreenBase>> Screens;

    ScreenBase GetFullScreen() {
        ScreenBase fullScreen{};
        fullScreen.ScreenSize = Render::HudCanvas->GetSize() / Render::HudCanvas->GetScale();
        return fullScreen;
    }

    // Returns a pointer to the screen
    ScreenBase* ShowScreen(Ptr<ScreenBase> screen) {
        screen->Layer = (int)Screens.size() * 2;
        screen->OnUpdateLayout();
        screen->OnUpdateLayout(); // Need to calculate layout twice due to sizing

        // Set initial selection based on how the screen was shown
        if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick))
            screen->SetSelection(screen->HitTestCursor());
        else
            screen->SelectFirst();

        Input::ResetState(); // Reset input to prevent clicking a control as soon as the screen appears
        screen->OnUpdate();
        Screens.push_back(std::move(screen));
        return Screens.back().get();
    }

    template <class TScreen>
    TScreen* ShowScreenT(Ptr<TScreen> screen) {
        screen->Layer = (int)Screens.size() * 2;
        screen->OnUpdateLayout();
        screen->OnUpdateLayout(); // Need to calculate layout twice due to sizing

        // Set initial selection based on how the screen was shown
        if (Input::IsMouseButtonDown(Input::MouseButtons::LeftClick))
            screen->SetSelection(screen->HitTestCursor());
        else
            screen->SelectFirst();

        Input::ResetState(); // Reset input to prevent clicking a control as soon as the screen appears
        screen->OnUpdate();
        Screens.push_back(std::move(screen));
        return (TScreen*)Screens.back().get();
    }

    template <class TScreen>
    TScreen* ShowScreenR(TScreen&& screen) {
        screen.Layer = (int)Screens.size() * 2;
        screen.OnUpdateLayout();
        screen.OnUpdateLayout(); // Need to calculate layout twice due to sizing

        // Set initial selection based on how the screen was shown
        if (Input::IsMouseButtonDown(Input::MouseButtons::LeftClick))
            screen.SetSelection(screen.HitTestCursor());
        else
            screen.SelectFirst();

        Input::ResetState(); // Reset input to prevent clicking a control as soon as the screen appears
        screen.OnUpdate();
        //Screens.push_back(std::forward<TScreen>(screen));
        //Screens.push_back( std::forward<TScreen>(screen));
        Screens.push_back(make_unique<TScreen>(std::forward<TScreen>(screen)));
        return (TScreen*)Screens.back().get();

        //Screens.push_back(std::move(screen));
    }

    bool CloseScreen() {
        if (Screens.size() == 1) return false; // Can't close the last screen

        auto& screen = Screens.back();
        SPDLOG_INFO("Closing screen {:x}", (int64)screen.get());
        if (screen->CloseCallback) screen->CloseCallback(screen->State);
        Seq::remove(Screens, screen);
        Input::ResetState(); // Clear state so clicking doesn't immediately trigger another action
        return true;
    }

    class DialogBase : public ScreenBase {
    public:
        DialogBase(string_view title = "") {
            HorizontalAlignment = AlignH::Center;
            VerticalAlignment = AlignV::Center;

            CloseButton close(CloseScreen);
            close.HorizontalAlignment = AlignH::Right;
            close.Margin = Vector2(DIALOG_MARGIN, DIALOG_MARGIN);
            AddChild(make_unique<CloseButton>(std::move(close)));

            if (!title.empty()) {
                Label titleLabel(title, FontSize::MediumBlue);
                titleLabel.VerticalAlignment = AlignV::Top;
                titleLabel.HorizontalAlignment = AlignH::Center;
                titleLabel.Position = Vector2(0, DIALOG_MARGIN);
                titleLabel.Color = DIALOG_TITLE_COLOR;
                AddChild(make_unique<Label>(std::move(titleLabel)));
            }
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
                Render::HudCanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Background
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition + border;
                cbi.Size = ScreenSize - border * 2;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = DIALOG_BACKGROUND;
                Render::HudCanvas->DrawBitmap(cbi, Layer);
            }

            //{
            //    // Header
            //    Render::CanvasBitmapInfo cbi;
            //    cbi.Position = ScreenPosition + border;
            //    cbi.Size = Vector2(ScreenSize.x - border.x * 2, 30 * GetScale());
            //    cbi.Texture = Render::Materials->White().Handle();
            //    cbi.Color = Color(0.02f, 0.02f, 0.02f, 1);
            //    Render::HudCanvas->DrawBitmap(cbi, 1);
            //}

            //{
            //    Render::DrawTextInfo dti;
            //    dti.Font = FontSize::Big;
            //    dti.HorizontalAlign = AlignH::Center;
            //    dti.VerticalAlign = AlignV::Top;
            //    dti.Position = Vector2(0, 20);
            //    dti.Color = Color(1, 1, 1, 1);
            //    Render::HudCanvas->DrawGameText("Header", dti, 2);
            //}

            ScreenBase::OnDraw();
        }
    };

    class LevelSelectDialog : public DialogBase {
        std::function<void(int)> _callback;
        gsl::strict_not_null<int*> _level;

    public:
        LevelSelectDialog(int levelCount, int& level) : DialogBase("select level"), _level(&level) {
            Size = Vector2(300, 170);

            Label description(fmt::format("1 to {}", levelCount), FontSize::MediumBlue);
            description.HorizontalAlignment = AlignH::Center;
            description.Position.y = 50;
            description.Color = DIALOG_TITLE_COLOR;

            Spinner levelSelect(1, levelCount, *_level);
            levelSelect.Position.y = 85;
            levelSelect.HorizontalAlignment = AlignH::Center;

            AddChild(make_unique<Label>(std::move(description)));
            AddChild(make_unique<Spinner>(std::move(levelSelect)));

            Button closeButton("ok", [this] { State = CloseState::Accept; });
            closeButton.HorizontalAlignment = AlignH::Center;
            closeButton.VerticalAlignment = AlignV::Bottom;
            closeButton.Margin = Vector2(0, DIALOG_MARGIN);
            AddChild(make_unique<Button>(std::move(closeButton)));
        }

        void OnUpdate() override {
            DialogBase::OnUpdate();

            if (Input::IsKeyPressed(Keys::Enter)) {
                Sound::Play2D(SoundResource{ MENU_SELECT_SOUND });
                State = CloseState::Accept;
            }
        }
    };

    using DifficultyCallback = std::function<void(DifficultyLevel)>;

    class DifficultyDialog : public DialogBase {
        gsl::strict_not_null<DifficultyLevel*> _value;

    public:
        DifficultyDialog(DifficultyLevel& value) : DialogBase("Difficulty"), _value(&value) {
            // immediately load briefing - no load screen
            // after briefing -> load screen "prepare for descent"

            Size = Vector2(260, 220);

            StackPanel panel;
            panel.Position = Vector2(0, 60);
            panel.HorizontalAlignment = AlignH::Center;
            panel.VerticalAlignment = AlignV::Top;

            panel.AddChild<Button>("Trainee", [this] { OnPick(DifficultyLevel::Trainee); });
            panel.AddChild<Button>("Rookie", [this] { OnPick(DifficultyLevel::Rookie); });
            panel.AddChild<Button>("Hotshot", [this] { OnPick(DifficultyLevel::Hotshot); });
            panel.AddChild<Button>("Ace", [this] { OnPick(DifficultyLevel::Ace); });
            Button insane("Insane", [this] { OnPick(DifficultyLevel::Insane); });
            insane.TextColor = Color(3.0f, 0.4f, 0.4f);
            insane.FocusColor = Color(4.0f, 0.4f, 0.4f);
            panel.AddChild<Button>(std::move(insane));

            //Button lunacy("Lunacy");
            //lunacy.TextColor = Color(4.0f, 0.4f, 0.4f);
            //panel.AddChild<Button>(std::move(lunacy));

            Children.push_back(make_unique<StackPanel>(std::move(panel)));
        }

        ControlBase* SelectFirst() override {
            // Terrible hack due to the lack of named controls
            if (auto list = Seq::tryItem(Children, 2)) {
                if (auto child = Seq::tryItem(list->get()->Children, (int)*_value)) {
                    SetSelection(child->get());
                    return child->get();
                }
            }

            return nullptr;
        }

        void OnPick(DifficultyLevel difficulty) {
            *_value = difficulty;
            State = CloseState::Accept;
        }
    };

    class PlayD1Dialog : public DialogBase {
        List<MissionInfo> _missions;
        DifficultyLevel _difficulty{};
        int _level = 1;

    public:
        PlayD1Dialog() {
            Size = Vector2(500, 460);

            _difficulty = Game::Difficulty;
            _missions = Resources::ReadMissionDirectory("./d1/missions");
            MissionInfo firstStrike{ .Name = "Descent: First Strike" };
            firstStrike.Levels.resize(27);
            _missions.insert(_missions.begin(), firstStrike);

            Label title("select mission", FontSize::MediumBlue);
            title.VerticalAlignment = AlignV::Top;
            title.HorizontalAlignment = AlignH::Center;
            title.Position = Vector2(0, DIALOG_MARGIN);
            title.Color = DIALOG_TITLE_COLOR;
            AddChild(make_unique<Label>(std::move(title)));

            ListBox missionList(14);

            for (auto& mission : _missions) {
                missionList.Items.push_back(mission.Name);
            }

            missionList.ClickItemAction = [this](int index) {
                if (auto mission = Seq::tryItem(_missions, index)) {
                    SPDLOG_INFO("Mission: {}", mission->Path.string());
                    Sound::Play2D(SoundResource{ ActionSound });

                    if (mission->Levels.size() > 1) {
                        ShowLevelSelect((int)mission->Levels.size());
                    }
                    else {
                        ShowDifficultySelect();
                    }
                }
            };

            missionList.Position = Vector2(30, 60);
            missionList.Size.x = 425;
            missionList.Padding = Vector2(10, 5);
            AddChild(make_unique<ListBox>(std::move(missionList)));
        }

    private:
        void ShowLevelSelect(int levels) {
            auto screen = ShowScreenT(make_unique<LevelSelectDialog>(levels, _level));

            screen->CloseCallback = [this](CloseState state) {
                if (state == CloseState::Accept)
                    ShowDifficultySelect();
            };
        }

        void ShowDifficultySelect() {
            auto screen = ShowScreen(make_unique<DifficultyDialog>(_difficulty));

            screen->CloseCallback =[this](CloseState state) {
                if(state == CloseState::Accept) {
                    // todo: show briefing or loading screen
                    Game::Difficulty = _difficulty;
                }
            };
        }
    };

    class MainMenu : public ScreenBase {
        //int _spinnerValue = 0;

    public:
        MainMenu() {
            StackPanel panel;
            panel.Position = Vector2(45, 140);
            panel.HorizontalAlignment = AlignH::CenterRight;
            panel.VerticalAlignment = AlignV::Top;

            //TextBox tb;
            //tb.Size = Vector2{ 200, 20 };
            //tb.Margin = Vector2{ 2, 2 };
            //tb.SetText("Hello");
            //panel.AddChild<TextBox>(std::move(tb));

            //Spinner spinner(0, 33, _spinnerValue);
            //spinner.Margin = Vector2{ 2, 2 };
            //spinner.SetValue(10);
            //panel.AddChild<Spinner>(std::move(spinner));

            panel.AddChild<Button>("Play Descent 1", [] {
                ShowScreen(make_unique<PlayD1Dialog>());
            });
            panel.AddChild<Button>("Play Descent 2");
            panel.AddChild<Button>("Load Game");
            panel.AddChild<Button>("Options");
            panel.AddChild<Button>("High Scores");
            panel.AddChild<Button>("Credits");
            panel.AddChild<Button>("Level Editor", [] {
                Game::SetState(GameState::Editor);
            });
            panel.AddChild<Button>("Quit", [] {
                PostMessage(Shell::Hwnd, WM_CLOSE, 0, 0);
            });

            AddChild(make_unique<StackPanel>(std::move(panel)));
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
                Render::HudCanvas->DrawText("inferno", dti);
            }

            {
                Render::DrawTextInfo dti;
                dti.Font = FontSize::Small;
                dti.HorizontalAlign = AlignH::Right;
                dti.VerticalAlign = AlignV::Bottom;
                dti.Position = Vector2(-5, -5);
                dti.Color = Color(0.25f, 0.25f, 0.25f);
                Render::HudCanvas->DrawText(APP_TITLE, dti);

                dti.Position.y -= 14;
                Render::HudCanvas->DrawText("software 1994, 1995, 1999", dti);

                dti.Position.y -= 14;
                Render::HudCanvas->DrawText("portions (c) parallax", dti);
            }
        }
    };

    void HandleInput() {
        if (Screens.empty()) return;
        auto& screen = Screens.back();

        // todo: add controller dpad input
        if (Input::IsKeyPressed(Input::Keys::Down, true))
            screen->OnDownArrow();

        if (Input::IsKeyPressed(Input::Keys::Up, true))
            screen->OnUpArrow();

        // Wrap selection
        //if (!screen->Children.empty())
        //    screen->SelectionIndex = (int)Mod(screen->SelectionIndex, screen->Children.size());

        if (Input::IsMouseButtonPressed(Input::MouseButtons::LeftClick))
            screen->OnClick(Input::MousePosition);

        // todo: add controller input
        if (Input::IsKeyPressed(Input::Keys::Enter) || Input::IsKeyPressed(Input::Keys::Space))
            screen->OnConfirm();

        if (Input::IsKeyPressed(Input::Keys::Escape)) {
            screen->State = CloseState::Cancel;
            return;
        }

        //if (screen.Controls.empty()) return;
        //auto& control = screen.Controls[screen.SelectionIndex];

        //if (Input::IsKeyPressed(Input::Keys::Enter)) {
        //    control.OnClick();
        //}
    }

    void DrawTestText(const Vector2& position, FontSize font, uchar lineLen = 32) {
        Render::DrawTextInfo dti;
        dti.Font = font;

        auto drawRange = [&](uchar min, uchar max, float yOffset) {
            string text;
            for (uchar i = min; i < max; i++)
                text += i;

            dti.Position = position;
            dti.Position.y += yOffset;
            Render::HudCanvas->DrawText(text, dti);
        };

        auto lineHeight = MeasureString("M", font).y;

        for (uchar i = 0; i < uchar(255) / lineLen; i++) {
            drawRange(i * lineLen, (i + 1) * lineLen, float(i * (lineHeight + 2)));
        }
    }

    void Update() {
        //DrawTestText({ 10, 0 }, FontSize::Medium);
        //DrawTestText({ 10, 150 }, FontSize::Small);
        //DrawTestText({ 10, 170 }, FontSize::Big, 24);

        if (Screens.empty()) {
            Screens.reserve(20);
            ShowScreen(make_unique<MainMenu>());
        }

        HandleInput();

        if (Screens.empty()) return;

        for (size_t i = 0; i < Screens.size(); i++) {
            auto& screen = Screens[i];

            if (i == Screens.size() - 1) {
                screen->OnUpdate(); // only update input for topmost screen
            }

            screen->OnUpdateLayout();
            screen->OnDraw();
        }

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

        if (Screens.back()->State == CloseState::Accept) {
            CloseScreen();
        }
        else if (Screens.back()->State == CloseState::Cancel) {
            if (CloseScreen())
                Sound::Play2D(SoundResource{ MENU_BACK_SOUND });
        }

        // debug outlines
        //debugDraw(*Screens.back().get());
    }
}
