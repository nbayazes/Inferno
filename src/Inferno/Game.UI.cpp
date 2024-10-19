#include "pch.h"
#include "Game.UI.h"
#include "Game.Text.h"
#include "Graphics/Render.h"
#include "Input.h"
#include "Types.h"
#include "Utility.h"
#include "Resources.h"
#include "Version.h"

namespace Inferno::UI {
    using Action = std::function<void(bool)>;
    using ClickHandler = std::function<void(const Vector2*)>;

    namespace {
        constexpr Color HOVER_COLOR = { 1, .9f, 0.9f };
        const auto FOCUS_COLOR = HOVER_COLOR * 1.7f;
        constexpr Color ACCENT_COLOR = { 1, .75f, .2f };
        const auto ACCENT_GLOW = ACCENT_COLOR * 2.0f;
        constexpr Color BORDER_COLOR = { 0.25f, 0.25f, 0.25f };
        constexpr Color IDLE_BUTTON = { 0.4f, 0.4f, 0.4f };
        constexpr Color DIALOG_TITLE_COLOR = { 1.25f, 1.25f, 2.0f };
        constexpr Color DIALOG_BACKGROUND = { 0.1f, 0.1f, 0.1f };

        constexpr float DIALOG_MARGIN = 15;
    }

    float GetScale() {
        return Render::HudCanvas->GetScale();
    }

    // Returns true if a rectangle at a position and size contain a point
    bool RectangleContains(const Vector2 position, const Vector2& size, const Vector2& point) {
        return
            point.x > position.x && point.x < position.x + size.x &&
            point.y > position.y && point.y < position.y + size.y;
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
                    control->ClickAction(true);
                    return;
                }

                control->OnClick(position);
            }
        }

        virtual void OnUpdate() {
            if (!Enabled) return;

            if (Selectable && Input::MouseMoved()) {
                Focused = Contains(Input::MousePosition);
                //Hovered = Contains(Input::MousePosition);
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
            Padding = Vector2(5, 5);
        }

        //int SelectionIndex = 0;

        ControlBase* Selection = nullptr;
        ControlBase* LastGoodSelection = nullptr;

        void OnUpdate() override {
            if (Input::MouseMoved())
                SetSelection(HitTestCursor());

            ControlBase::OnUpdate(); // breaks main menu selection
        }

        void OnConfirm() const {
            if (Selection && Selection->ClickAction)
                Selection->ClickAction(false);
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

            if (control) {
                control->Focused = true;
                LastGoodSelection = Selection;
            }
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

            if (index == -1)
                return; // selection not found

            if (index == 0)
                SetSelection(tree.back()); // wrap
            else
                SetSelection(tree[index - 1]);
        }

        void OnDownArrow() {
            List<ControlBase*> tree;
            FlattenSelectionTree(tree);
            int index = FindSelectionIndex(tree);

            if (index == -1)
                return; // selection not found

            if (index == tree.size() - 1)
                SetSelection(tree.front()); // wrap
            else
                SetSelection(tree[index + 1]);
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
        FontSize _size;

    public:
        Color Color = { 1, 1, 1 };

        Label(string_view text, FontSize size = FontSize::Medium) : _text(text), _size(size) {
            Selectable = false;
        }

        void OnUpdateLayout() override {
            Size = MeasureString(_text, _size);
        }

        void OnDraw() override {
            Render::DrawTextInfo dti;
            dti.Font = _size;
            dti.Color = Color;
            dti.Position = ScreenPosition / GetScale() + Margin;
            Render::HudCanvas->DrawGameText(_text, dti, Layer);
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
            dti.Color = Focused || Hovered ? FocusColor : TextColor;
            dti.Position = ScreenPosition / GetScale() + Padding;
            Render::HudCanvas->DrawGameText(_text, dti, Layer);
        }
    };

    class CloseButton : public ControlBase {
    public:
        CloseButton(Action&& action) {
            ClickAction = action;
            Size = Vector2(15, 15);
            Selectable = false; // Disable keyboard navigation
        }

        float Thickness = 1.0f;

        void OnDraw() override {
            const float thickness = Thickness * GetScale();

            Render::HudCanvasPayload payload;
            payload.Texture = Render::Materials->White().Handle();
            payload.Layer = Layer;
            payload.V0.Color = payload.V1.Color = payload.V2.Color = payload.V3.Color = Focused ? ACCENT_GLOW : IDLE_BUTTON;

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
                    child->Layer = Layer + 1;
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
                    child->Layer = Layer + 1;
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

        std::function<void(int, bool)> ClickItemAction;

        ListBox(int visibleItems) : VisibleItems(visibleItems) {
            _fontHeight = MeasureString("Descent", FontSize::Medium).y;

            Rectangle rect;
            AddChild(make_unique<Rectangle>(std::move(rect)));
            Padding = Vector2(2, 2);
            //auto list = make_unique<StackPanel>();
            //_list = list.get();
            //AddChild(std::move(list));
        }

        //void AddItem(string_view item) const {
        //    Label label(item, _font);
        //    label.Margin = Vector2(Margin, Margin);
        //    _list->AddChild(make_unique<Label>(std::move(label)));
        //}

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
                ClickItemAction(_index, true);
            }

            _index = std::clamp(_index, 0, (int)Items.size() - 1);
            _scrollIndex = std::clamp(_scrollIndex, 0, std::max((int)Items.size() - VisibleItems, 0));

            if (wheelDelta != 0)
                HitTestCursor(); // Update index when scrolling

            if (Input::IsKeyPressed(Keys::Enter) && ClickItemAction) {
                ClickItemAction(_index, false);
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
                Render::HudCanvas->DrawGameText(item, dti, Layer + 1);
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

    void ShowScreen(Ptr<ScreenBase> screen, bool mouseClick = true) {
        Input::ResetState(); // Reset input to prevent clicking a control as soon as the screen appears
        screen->Layer = (int)Screens.size() * 2;
        screen->OnUpdateLayout();
        screen->OnUpdateLayout(); // Need to calculate layout twice due to sizing

        // Set initial selection based on how the screen was shown
        if (!mouseClick)
            screen->SelectFirst();
        else
            screen->SetSelection(screen->HitTestCursor());

        screen->OnUpdate();
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
        Input::ResetState(); // Clear state so clicking doesn't immediately trigger another action
    }

    class DialogBase : public ScreenBase {
    public:
        DialogBase(string_view title = "") {
            CloseButton close([](bool) { CloseScreen(); });
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


            ScreenBase::OnDraw();
        }
    };

    class LevelSelectDialog : public DialogBase {
    public:
        LevelSelectDialog() : DialogBase("Level select") {
            // new level (blue)
            // select starting level (blue)
            // you may start on any level up to {n}
            // [textbox] input, cursor. left arrow deletes
            // check invalid input
        }

        void OnDraw() override {
            //Render::CanvasBitmapInfo cbi;
            //cbi.Position = ScreenPosition;
            //cbi.Size = ScreenSize;
            //cbi.Texture = Render::Materials->White().Handle();
            //cbi.Color = Color(0.1f, 0.1f, 0.1f, 1);
            //Render::HudCanvas->DrawBitmap(cbi, 1);
        }
    };

    class DifficultyDialog : public DialogBase {
    public:
        DifficultyDialog() : DialogBase("Difficulty") {
            // difficulty level (blue)
            // five difficulties in list (same as main menu)

            // immediately load briefing - no load screen
            // after briefing -> load screen "prepare for descent"

            HorizontalAlignment = AlignH::Center;
            VerticalAlignment = AlignV::Center;

            Size = Vector2(260, 220);

            StackPanel panel;
            panel.Position = Vector2(0, 60);
            panel.HorizontalAlignment = AlignH::Center;
            panel.VerticalAlignment = AlignV::Top;

            panel.AddChild<Button>("Trainee");
            panel.AddChild<Button>("Rookie");
            panel.AddChild<Button>("Hotshot");
            panel.AddChild<Button>("Ace");
            Button insane("Insane");
            insane.TextColor = Color(3.5f, 0.4f, 0.4f);
            insane.FocusColor = Color(4.4f, 0.4f, 0.4f);
            panel.AddChild<Button>(std::move(insane));

            //Button lunacy("Lunacy");
            //lunacy.TextColor = Color(4.0f, 0.4f, 0.4f);
            //panel.AddChild<Button>(std::move(lunacy));

            Children.push_back(make_unique<StackPanel>(std::move(panel)));
        }
    };

    class PlayD1Dialog : public DialogBase {
        List<MissionInfo> _missions;

    public:
        PlayD1Dialog() {
            HorizontalAlignment = AlignH::Center;
            VerticalAlignment = AlignV::Center;
            Size = Vector2(500, 460);

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

            missionList.ClickItemAction = [this](int index, bool mouseClick) {
                if (auto mission = Seq::tryItem(_missions, index)) {
                    SPDLOG_INFO("Mission: {}", mission->Path.string());
                    ShowScreen(make_unique<DifficultyDialog>(), mouseClick);
                    //ShowScreen(make_unique<PlayD1Dialog>());


                    //if (mission->Levels.size() > 1) {
                    //    ShowScreen(make_unique<LevelSelectDialog>());
                    //}
                    //else {
                    //    ShowScreen(make_unique<DifficultyDialog>());
                    //}
                }
            };

            missionList.Position = Vector2(30, 60);
            missionList.Size.x = 425;
            missionList.Padding = Vector2(10, 5);
            AddChild(make_unique<ListBox>(std::move(missionList)));
        }
    };

    class MainMenu : public ScreenBase {
    public:
        MainMenu() {
            StackPanel panel;
            panel.Position = Vector2(45, 140);
            panel.HorizontalAlignment = AlignH::CenterRight;
            panel.VerticalAlignment = AlignV::Top;

            panel.AddChild<Button>("Play Descent 1", [](bool mouseClick) {
                ShowScreen(make_unique<PlayD1Dialog>(), mouseClick);
            });
            panel.AddChild<Button>("Play Descent 2");
            panel.AddChild<Button>("Load Game");
            panel.AddChild<Button>("Options");
            panel.AddChild<Button>("High Scores");
            panel.AddChild<Button>("Credits");
            panel.AddChild<Button>("Level Editor", [](bool) {
                Game::SetState(GameState::Editor);
            });
            panel.AddChild<Button>("Quit", [](bool) {
                PostMessage(Shell::Hwnd, WM_CLOSE, 0, 0);
            });

            Children.push_back(make_unique<StackPanel>(std::move(panel)));
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
            Render::HudCanvas->DrawGameText(APP_TITLE, dti);

            dti.Position.y -= 14;
            Render::HudCanvas->DrawGameText("software 1994, 1995, 1999", dti);

            dti.Position.y -= 14;
            Render::HudCanvas->DrawGameText("portions (c) parallax", dti);
        }

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

        // debug outlines
        //debugDraw(*Screens.back().get());
    }
}
