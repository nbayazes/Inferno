#include "pch.h"
#include "Game.UI.h"
#include "Game.UI.Controls.h"
#include "Game.Text.h"
#include "Game.UI.Options.h"
#include "Graphics.h"
#include "Graphics/Render.h"
#include "gsl/pointers.h"
#include "Input.h"
#include "Types.h"
#include "Utility.h"
#include "Resources.h"
#include "SoundSystem.h"
#include "Version.h"

namespace Inferno::UI {
    using ClickHandler = std::function<void(const Vector2*)>;
    using Inferno::Input::Keys;

    namespace {
        bool CursorCaptured = false;
    }

    void CaptureCursor(bool capture) { CursorCaptured = capture; }
    bool IsCursorCaptured() { return CursorCaptured; }

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
                Render::UICanvas->DrawBitmap(cbi, Layer);
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
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                Render::DrawTextInfo dti;
                dti.Font = Focused ? FontSize::MediumGold : _font;
                dti.Color = Focused /*|| Hovered*/ ? FocusColor : TextColor;
                dti.Position = ScreenPosition / GetScale() + Margin + Padding;
                dti.EnableTokenParsing = false;
                Render::UICanvas->DrawText(_text, dti, Layer + 1);
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
                Render::UICanvas->DrawText("_", dti, Layer + 1);
            }
        }
    };

    List<Ptr<ScreenBase>> Screens;

    ScreenBase GetFullScreen() {
        ScreenBase fullScreen{};
        fullScreen.ScreenSize = Render::UICanvas->GetSize() / Render::UICanvas->GetScale();
        return fullScreen;
    }

    // Returns a pointer to the screen
    ScreenBase* ShowScreen(Ptr<ScreenBase> screen) {
        if (screen->Layer == -1) screen->Layer = (int)Screens.size() * 2;
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

    bool CloseScreen() {
        if (Screens.size() == 1) {
            if (!Screens.back()->OnTryClose())
                return false; // Can't close the last screen
        }

        auto& screen = Screens.back();
        SPDLOG_INFO("Closing screen {:x}", (int64)screen.get());
        screen->OnClose();
        if (screen->CloseCallback) screen->CloseCallback(screen->State);
        Seq::remove(Screens, screen); // Remove the  original screen because the callback might open a new one
        Input::ResetState(); // Clear state so clicking doesn't immediately trigger another action
        CaptureCursor(false);
        return true;
    }

    class LevelSelectDialog : public DialogBase {
        std::function<void(int)> _callback;
        gsl::strict_not_null<int*> _level;

    public:
        LevelSelectDialog(int levelCount, int& level) : DialogBase("select level"), _level(&level) {
            Size = Vector2(300, 170);

            auto description = make_unique<Label>(fmt::format("1 to {}", levelCount), FontSize::MediumBlue);
            description->HorizontalAlignment = AlignH::Center;
            description->Position.y = 50;
            description->Color = DIALOG_TITLE_COLOR;

            auto levelSelect = make_unique<Spinner>(1, levelCount, *_level);
            levelSelect->Position.y = 85;
            levelSelect->HorizontalAlignment = AlignH::Center;

            AddChild(std::move(description));
            AddChild(std::move(levelSelect));

            auto closeButton = make_unique<Button>("ok", [this] {
                State = CloseState::Accept;
            });
            closeButton->HorizontalAlignment = AlignH::Center;
            closeButton->VerticalAlignment = AlignV::Bottom;
            closeButton->Margin = Vector2(0, DIALOG_PADDING);
            AddChild(std::move(closeButton));
        }

        void OnUpdate() override {
            DialogBase::OnUpdate();

            if (Input::IsKeyPressed(Keys::Enter)) {
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

            auto panel = make_unique<StackPanel>();
            panel->Position = Vector2(0, 60);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;

            panel->AddChild<Button>("Trainee", [this] { OnPick(DifficultyLevel::Trainee); });
            panel->AddChild<Button>("Rookie", [this] { OnPick(DifficultyLevel::Rookie); });
            panel->AddChild<Button>("Hotshot", [this] { OnPick(DifficultyLevel::Hotshot); });
            panel->AddChild<Button>("Ace", [this] { OnPick(DifficultyLevel::Ace); });
            Button insane("Insane", [this] { OnPick(DifficultyLevel::Insane); });
            insane.TextColor = Color(3.0f, 0.4f, 0.4f);
            insane.FocusColor = Color(4.0f, 0.4f, 0.4f);
            panel->AddChild<Button>(std::move(insane));

            //Button lunacy("Lunacy");
            //lunacy.TextColor = Color(4.0f, 0.4f, 0.4f);
            //panel.AddChild<Button>(std::move(lunacy));

            Children.push_back(std::move(panel));
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

    class ConfirmDialog : public DialogBase {
        gsl::strict_not_null<bool*> _result;

    public:
        ConfirmDialog(string_view message, bool& result) : DialogBase("", false), _result(&result) {
            auto label = make_unique<Label>(message, FontSize::MediumBlue);
            label->HorizontalAlignment = AlignH::Center;
            label->Position = Vector2(0, DIALOG_PADDING);

            Size = MeasureString(message, FontSize::Medium);
            Size.x += DIALOG_PADDING * 2 + 20;
            Size.y = Size.y * 2 + DIALOG_PADDING * 2 + 10;

            auto yesButton = make_unique<Button>("yes");
            yesButton->VerticalAlignment = AlignV::Bottom;
            yesButton->HorizontalAlignment = AlignH::Center;
            yesButton->Position = Vector2(-50, -DIALOG_PADDING);
            yesButton->ClickAction = [this] { State = CloseState::Accept; };

            auto noButton = make_unique<Button>("no");
            noButton->VerticalAlignment = AlignV::Bottom;
            noButton->HorizontalAlignment = AlignH::Center;
            noButton->Position = Vector2(50, -DIALOG_PADDING);
            noButton->ActionSound = "";
            noButton->ClickAction = [this] { State = CloseState::Cancel; };

            AddChild(std::move(label));
            AddChild(std::move(yesButton));
            AddChild(std::move(noButton));
        }

        void OnUpdate() override {
            DialogBase::OnUpdate();

            if (Input::IsKeyPressed(Keys::Left)) OnUpArrow();
            if (Input::IsKeyPressed(Keys::Right)) OnDownArrow();
        }

        bool OnTryClose() override {
            Game::SetState(GameState::Game);
            return true; // Allow closing this dialog with escape
        }
    };

    constexpr auto FIRST_STRIKE_NAME = "Descent: First Strike";

    class PlayD1Dialog : public DialogBase {
        List<MissionInfo> _missions;
        DifficultyLevel _difficulty{};
        int _level = 1;
        MissionInfo* _mission = nullptr;

    public:
        PlayD1Dialog() {
            Size = Vector2(500, 460);
            CloseOnConfirm = false;

            _difficulty = Game::Difficulty;
            _missions = Resources::ReadMissionDirectory("d1/missions");
            MissionInfo firstStrike{ .Name = FIRST_STRIKE_NAME, .Path = "d1/descent.hog" };
            firstStrike.Levels.resize(27);
            for (int i = 1; i <= firstStrike.Levels.size(); i++) {
                // todo: this could also be SDL
                firstStrike.Levels[i - 1] = fmt::format("level{:02}.rdl", i);
            }

            firstStrike.Metadata["briefing"] = "briefing";
            firstStrike.Metadata["ending"] = "ending";
            _missions.insert(_missions.begin(), firstStrike);

            auto title = make_unique<Label>("select mission", FontSize::MediumBlue);
            title->VerticalAlignment = AlignV::Top;
            title->HorizontalAlignment = AlignH::Center;
            title->Position = Vector2(0, DIALOG_PADDING);
            title->Color = DIALOG_TITLE_COLOR;
            AddChild(std::move(title));

            auto missionList = std::make_unique<ListBox>(14);

            for (auto& mission : _missions) {
                missionList->Items.push_back(mission.Name);
            }

            missionList->ClickItemAction = [this](int index) {
                if (auto mission = Seq::tryItem(_missions, index)) {
                    SPDLOG_INFO("Mission: {}", mission->Path.string());
                    _mission = mission;

                    if (mission->Levels.size() > 1) {
                        ShowLevelSelect((int)mission->Levels.size());
                    }
                    else {
                        ShowDifficultySelect();
                    }
                }
            };

            missionList->Position = Vector2(30, 60);
            missionList->Size.x = 425;
            missionList->Padding = Vector2(10, 5);
            AddChild(std::move(missionList));
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
                        filesystem::path hogPath = _mission->Path;
                        hogPath.replace_extension(".hog");

                        if (!Game::LoadMission(hogPath)) {
                            ShowErrorMessage(Convert::ToWideString(std::format("Unable to load mission {}", hogPath.string())));
                            return;
                        }

                        auto isShareware = Game::Mission->ContainsFileType(".sdl");
                        auto levelEntry = Seq::tryItem(_mission->Levels, _level - 1);
                        if (!levelEntry) {
                            ShowErrorMessage(Convert::ToWideString(std::format("Tried to load level {} but hog only contains {}", _level, _mission->Levels.size())));
                            return;
                        }

                        auto data = Game::Mission->ReadEntry(*levelEntry);
                        auto level = isShareware ? Level::DeserializeD1Demo(data) : Level::Deserialize(data);
                        //Game::LoadLevelFromMission(_mission->Levels[_level]);
                        Resources::LoadLevel(level);
                        Graphics::LoadLevel(level);
                        Game::LoadLevel(hogPath, *levelEntry);

                        auto briefingName = _mission->GetValue("briefing");

                        if (!briefingName.empty()) {
                            if (String::Extension(briefingName).empty())
                                briefingName += ".txb";

                            auto entry = Game::Mission->TryReadEntry(briefingName);
                            auto briefing = Briefing::Read(entry);

                            // mount the game data
                            //if (isShareware)
                            //    Resources::LoadDescent1Shareware();
                            //else
                            //    Resources::LoadDescent1Resources();

                            SetD1BriefingBackgrounds(briefing, isShareware);

                            // Queue load level

                            if (_mission->Name == FIRST_STRIKE_NAME && _level == 1) {
                                AddPyroAndReactorPages(briefing);
                            }

                            //LoadBriefingResources(briefing);
                            Game::Briefing = BriefingState(briefing, _level, true);
                            Game::Level.Version = level.Version; // hack: due to LoadResources
                            Game::Briefing.LoadResources(); // TODO: Load resources depends on the level being fully loaded to pick the right assets!
                            Game::PlayMusic("d1/briefing");
                            Game::SetState(GameState::Briefing);
                        }
                        else {
                            Game::SetState(GameState::LoadLevel);
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
    public:
        MainMenu() {
            CloseOnConfirm = false;

            auto panel = make_unique<StackPanel>();
            panel->Position = Vector2(45, 140);
            panel->HorizontalAlignment = AlignH::CenterRight;
            panel->VerticalAlignment = AlignV::Top;

            //TextBox tb;
            //tb.Size = Vector2{ 200, 20 };
            //tb.Margin = Vector2{ 2, 2 };
            //tb.SetText("Hello");
            //panel->AddChild<TextBox>(std::move(tb));

            //Spinner spinner(0, 33, _spinnerValue);
            //spinner.Margin = Vector2{ 2, 2 };
            //spinner.SetValue(10);
            //panel->AddChild<Spinner>(std::move(spinner));

            panel->AddChild<Button>("Play Descent 1", [] {
                ShowScreen(make_unique<PlayD1Dialog>());
            });
            panel->AddChild<Button>("Play Descent 2");
            panel->AddChild<Button>("Load Game");
            panel->AddChild<Button>("Options", [] {
                ShowScreen(make_unique<OptionsMenu>());
            });
            panel->AddChild<Button>("High Scores");
            panel->AddChild<Button>("Credits");
            panel->AddChild<Button>("Level Editor", [] {
                Game::SetState(GameState::Editor);
            });
            panel->AddChild<Button>("Quit", [] {
                PostMessage(Shell::Hwnd, WM_CLOSE, 0, 0);
            });

            AddChild(std::move(panel));
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

                //Render::UICanvas->DrawGameText("descent remastered", dti);
                //dti.Position.y += 15;
                //Render::UICanvas->DrawGameText("descent II", dti);
                //dti.Position.y += 15;
                //Render::UICanvas->DrawGameText("descent 3 enhancements enabled", dti);
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
                Render::UICanvas->DrawText("inferno", dti);
            }

            {
                Render::DrawTextInfo dti;
                dti.Font = FontSize::Small;
                dti.HorizontalAlign = AlignH::Right;
                dti.VerticalAlign = AlignV::Bottom;
                dti.Position = Vector2(-5, -5);
                dti.Color = Color(0.25f, 0.25f, 0.25f);
                Render::UICanvas->DrawText(APP_TITLE, dti);

                dti.Position.y -= 14;
                Render::UICanvas->DrawText("software 1994, 1995, 1999", dti);

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

    class PauseMenu : public DialogBase {
        bool _quitConfirm = false;
        float _topOffset = 150;
        Vector2 _menuSize;
    public:
        PauseMenu() : DialogBase("", false) {
            CloseOnConfirm = false;

            auto panel = make_unique<StackPanel>();
            panel->Position = Vector2(0, _topOffset);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;

            panel->AddChild<Button>("Continue", [] {
                Game::SetState(GameState::Game);
            }, AlignH::Center);
            panel->AddChild<Button>("Save Game", AlignH::Center);
            panel->AddChild<Button>("Load Game", AlignH::Center);
            panel->AddChild<Button>("Options", [] {
                ShowScreen(make_unique<OptionsMenu>());
            }, AlignH::Center);
            panel->AddChild<Button>("Quit", [this] {
                auto confirmDialog = make_unique<ConfirmDialog>("are you sure?", _quitConfirm);
                confirmDialog->CloseCallback = [this](CloseState state) {
                    if (state == CloseState::Accept)
                        Game::SetState(GameState::MainMenu);
                };

                ShowScreen(std::move(confirmDialog));
            }, AlignH::Center);

            _menuSize = MeasureString("Load Game", FontSize::Medium);
            _menuSize.y *= (float)panel->Children.size();

            //auto size = MeasureString("Load Game", FontSize::Medium);
            //Size = Vector2(size.x + 80, size.y * panel->Children.size() + 40 + panel->Position.y);

            AddChild(std::move(panel));
            Sound::Play2D(SoundResource{ MENU_SELECT_SOUND });
        }

        bool OnTryClose() override {
            Game::SetState(GameState::Game);
            return true; // Allow closing this dialog with escape
        }

        void OnDraw() override {
            {
                // Background
                Render::CanvasBitmapInfo cbi;
                cbi.Size = ScreenSize / Settings::Graphics.RenderScale;
                cbi.Texture = Render::Adapter->BlurBufferDownsampled.GetSRV();
                cbi.Color = Color(.5f, .5f, .5f, 1);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Text Background
                auto& material = Render::Materials->Get("menu-bg");

                Render::CanvasBitmapInfo cbi;
                cbi.Position.y = (_topOffset - 40) * GetScale();
                cbi.Size = Vector2(400, _menuSize.y + 80) * GetScale();
                cbi.Texture = material.Handle();
                cbi.Color = Color(1, 1, 1, 0.70f);
                cbi.HorizontalAlign = AlignH::Center;
                cbi.VerticalAlign = AlignV::Top;
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            ScreenBase::OnDraw();
        }

        //void OnDialogClose() override {
        //    Game::SetState(GameState::Game);
        //    CloseScreen();
        //}
    };

    void ShowMainMenu() {
        if (Screens.empty()) {
            Screens.reserve(20);
        }

        Screens.clear();
        ShowScreen(make_unique<MainMenu>());

        ShowScreen(make_unique<OptionsMenu>());
    }

    void ShowPauseDialog() {
        if (Screens.empty()) {
            Screens.reserve(20);
        }

        Screens.clear();
        ShowScreen(make_unique<PauseMenu>());
    }

    void Update() {
        //DrawTestText({ 10, 0 }, FontSize::Medium);
        //DrawTestText({ 10, 150 }, FontSize::Small);
        //DrawTestText({ 10, 170 }, FontSize::Big, 24);


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

        if (Screens.back()->State == CloseState::Accept) {
            CloseScreen();
            /*string sound = Screens.back()->ActionSound;
            if (CloseScreen())
                Sound::Play2D(SoundResource{ sound });*/
        }
        else if (Screens.back()->State == CloseState::Cancel) {
            if (CloseScreen())
                Sound::Play2D(SoundResource{ MENU_BACK_SOUND });
        }

        std::function<void(ControlBase&)> debugDraw = [&](const ControlBase& control) {
            for (auto& child : control.Children) {
                Render::CanvasBitmapInfo cbi;
                cbi.Position = child->ScreenPosition;
                cbi.Size = child->ScreenSize;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Color(0.1f, 1.0f, 0.1f, 0.0225f);
                Render::UICanvas->DrawBitmap(cbi, 9);

                debugDraw(*child.get());
            }
        };

        // debug outlines
        //debugDraw(*Screens.back().get());
    }
}
