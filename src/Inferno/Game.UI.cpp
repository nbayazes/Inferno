#include "pch.h"
#include "Game.UI.h"
#include "Game.UI.Controls.h"
#include "Game.Text.h"
#include "Graphics.h"
#include "Graphics/Render.h"
#include "gsl/pointers"
#include "Input.h"
#include "Types.h"
#include "Utility.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Version.h"

namespace Inferno::UI {
    using ClickHandler = std::function<void(const Vector2*)>;
    using Inferno::Input::Keys;

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
        MissionInfo* _mission = nullptr;

    public:
        PlayD1Dialog() {
            Size = Vector2(500, 460);

            _difficulty = Game::Difficulty;
            _missions = Resources::ReadMissionDirectory("d1/missions");
            MissionInfo firstStrike{ .Name = "Descent: First Strike", .Path = "d1/descent.hog" };
            firstStrike.Levels.resize(27);
            for (int i = 0; i < firstStrike.Levels.size(); i++) {
                // todo: this could also be SDL
                firstStrike.Levels[i] = fmt::format("level{:02}.rdl", i);
            }

            firstStrike.Metadata["briefing"] = "briefing.txb";
            firstStrike.Metadata["ending"] = "ending.txb";
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
                    _mission = mission;

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

            screen->CloseCallback = [this](CloseState state) {
                if (state == CloseState::Accept && _mission) {
                    try {
                        // todo: show briefing or loading screen
                        Game::Difficulty = _difficulty;

                        // open the hog and check for a briefing
                        if (!Game::LoadMission(_mission->Path)) {
                            ShowErrorMessage(Convert::ToWideString(
                                std::format("Unable to load mission {}", _mission->Path.string())
                            ));
                            return;
                        }

                        auto briefingName = _mission->GetValue("briefing");

                        if (!briefingName.empty()) {
                            auto entry = Game::Mission->ReadEntry(briefingName);
                            auto briefing = Briefing::Read(entry);
                            auto isShareware = Game::Mission->ContainsFileType(".sdl");

                            // mount the game data
                            //if (isShareware)
                            //    Resources::LoadDescent1Shareware();
                            //else
                            //    Resources::LoadDescent1Resources();

                            SetD1BriefingBackgrounds(briefing, isShareware);
                            //auto level = Game::Mission->TryReadEntry(_mission->Levels[_level]));
                            //if(!level.empty())

                            auto data = Game::Mission->ReadEntry(_mission->Levels[_level]);
                            auto level = isShareware ? Level::DeserializeD1Demo(data) : Level::Deserialize(data);
                            //Game::LoadLevelFromMission(_mission->Levels[_level]);
                            //level.FileName = _mission->Levels[_level];
                            Resources::LoadLevel(level);
                            Graphics::LoadLevel(level);

                            //Game::InitLevel(std::move(level));

                            // Queue load level
                            Game::LoadLevel(_mission->Path, _mission->Levels[_level]);
                            
                            //Resources::LoadLevel(); // preferably this would only load the game resources

                            // todo: load exit door
                            // todo: load base models and textures

                            //AddPyroAndReactorPages(_briefing);
                            //OpenBriefing(entry);

                            ResolveBriefingImages(briefing);
                            Game::Briefing = BriefingState(briefing, 0, true);

                            List<TexID> ids;

                            for (auto& screen : briefing.Screens) {
                                for (auto& page : screen.Pages) {
                                    auto& doorClip = Resources::GetDoorClip(page.Door);
                                    auto wids = Seq::map(doorClip.GetFrames(), Resources::LookupTexID);
                                    Seq::append(ids, wids);
                                }
                            }

                            Game::LoadBackgrounds(*Game::Mission);
                            Graphics::LoadTextures(ids);

                            Game::PlayMusic("d1/briefing");
                            Game::SetState(GameState::Briefing);
                        }
                    }
                    catch (const std::exception& e) {
                        ShowErrorMessage(Convert::ToWideString(
                            std::format("Unable to load mission {}\n{}", _mission->Path.string(), e.what())
                        ));
                    }
                }

                _mission = nullptr;
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
