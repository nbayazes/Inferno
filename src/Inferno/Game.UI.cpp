#include "pch.h"
#include "Game.UI.h"
#include "Game.UI.Controls.h"
#include "Game.Text.h"
#include "Game.UI.LoadDialog.h"
#include "Game.UI.Options.h"
#include "Game.UI.ScoreScreen.h"
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
        bool InputCaptured = false;
    }

    void CaptureCursor(bool capture) { CursorCaptured = capture; }
    bool IsCursorCaptured() { return CursorCaptured; }

    void CaptureInput(bool capture) { InputCaptured = capture; }
    bool IsInputCaptured() { return InputCaptured; }

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

    // Returns a pointer to the screen
    ScreenBase* ShowScreen(Ptr<ScreenBase> screen) {
        if (Screens.empty()) {
            Screens.reserve(20);
        }

        if (screen->Layer == -1) screen->Layer = (int)Screens.size() * 2;
        screen->OnUpdateLayout();
        screen->OnUpdateLayout(); // Need to calculate layout twice due to sizing

        // Set initial selection based on how the screen was shown
        if (Input::MouseButtonPressed(Input::MouseButtons::LeftClick))
            screen->SetSelection(screen->HitTestCursor());
        else
            screen->SelectFirst();

        Input::ResetState(); // Reset input to prevent clicking a control as soon as the screen appears
        screen->OnShow();
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
        if (Screens.empty()) return false;

        if (Screens.size() == 1) {
            if (!Screens.back()->OnTryClose())
                return false; // Can't close the last screen
        }

        if (Screens.back()->State == CloseState::Accept)
            Sound::Play2D(SoundResource{ Screens.back()->ActionSound });
        else if (Screens.back()->State == CloseState::Cancel)
            Sound::Play2D(SoundResource{ MENU_BACK_SOUND });

        auto& screen = Screens.back();
        //SPDLOG_INFO("Closing screen {:x}", (int64)screen.get());
        screen->OnClose();
        if (screen->CloseCallback) screen->CloseCallback(screen->State);
        Seq::remove(Screens, screen); // Remove the original screen because the callback might open a new one
        Input::ResetState(); // Clear state so clicking doesn't immediately trigger another action
        CaptureCursor(false);
        return true;
    }

    void SetSelection(class ControlBase* control) {
        if (Screens.empty()) return;

        auto& screen = Screens.back();
        screen->SetSelection(control);
    }

    class LevelSelectDialog : public DialogBase {
        std::function<void(int)> _callback;
        gsl::strict_not_null<int*> _level;

    public:
        LevelSelectDialog(int levelCount, int& level) : DialogBase("select level", false), _level(&level) {
            Size = Vector2(_titleSize.x + DIALOG_PADDING * 2, 170);
            CloseOnClickOutside = true;

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
            closeButton->ActionSound = ""; // closes the dialog, no need for sound
            AddChild(std::move(closeButton));
        }

        void OnUpdate() override {
            DialogBase::OnUpdate();

            if (Input::MenuActions.IsSet(MenuAction::Confirm)) {
                State = CloseState::Accept;
            }
        }
    };

    using DifficultyCallback = std::function<void(DifficultyLevel)>;

    class DifficultyDialog : public DialogBase {
        gsl::strict_not_null<DifficultyLevel*> _value;
        Button* _selection;

    public:
        DifficultyDialog(DifficultyLevel& value) : DialogBase("Difficulty", false), _value(&value) {
            Size = Vector2(_titleSize.x + DIALOG_PADDING * 2, CONTROL_HEIGHT * 5 + DIALOG_HEADER_PADDING + DIALOG_PADDING);
            CloseOnClickOutside = true;
            ActionSound = ""; // Clear close sound because buttons already have one

            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x;
            panel->Position = Vector2(2, DIALOG_HEADER_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;

            auto trainee = panel->AddChild<Button>("Trainee", [this] { OnPick(DifficultyLevel::Trainee); }, AlignH::Center);
            auto rookie = panel->AddChild<Button>("Rookie", [this] { OnPick(DifficultyLevel::Rookie); }, AlignH::Center);
            auto hotshot = panel->AddChild<Button>("Hotshot", [this] { OnPick(DifficultyLevel::Hotshot); }, AlignH::Center);
            auto ace = panel->AddChild<Button>("Ace", [this] { OnPick(DifficultyLevel::Ace); }, AlignH::Center);
            auto insane = panel->AddChild<Button>("Insane", [this] { OnPick(DifficultyLevel::Insane); }, AlignH::Center);
            insane->TextColor = INSANE_TEXT;
            insane->FocusColor = INSANE_TEXT_FOCUSED;

            //Button lunacy("Lunacy");
            //lunacy.TextColor = Color(4.0f, 0.4f, 0.4f);
            //panel.AddChild<Button>(std::move(lunacy));

            // Set default selection based on difficulty
            switch (Game::Difficulty) {
                default:
                case DifficultyLevel::Trainee:
                    _selection = trainee;
                    break;
                case DifficultyLevel::Rookie:
                    _selection = rookie;
                    break;
                case DifficultyLevel::Hotshot:
                    _selection = hotshot;
                    break;
                case DifficultyLevel::Ace:
                    _selection = ace;
                    break;
                case DifficultyLevel::Insane:
                    _selection = insane;
                    break;
            }

            Children.push_back(std::move(panel));
        }

        ControlBase* SelectFirst() override {
            SetSelection(_selection);
            return _selection;
        }

        void OnPick(DifficultyLevel difficulty) {
            *_value = difficulty;
            State = CloseState::Accept;
        }
    };

    class FailedEscapeDialog : public DialogBase {
    public:
        FailedEscapeDialog(bool missionFailed) : DialogBase("You didn't escape in time", false) {
            ActionSound = "";
            Layer = 1; // 0 Is for white background

            //auto title = AddChild<Label>("You didn't escape in time", FontSize::MediumBlue);
            //title->HorizontalAlignment = AlignH::Center;
            //title->Position = Vector2(0, DIALOG_PADDING);
            //title->TextAlignment = AlignH::Center;
            //title->Color = DIALOG_TITLE_COLOR;

            auto text = AddChild<Label>("Your ship and its", FontSize::MediumBlue);
            text->HorizontalAlignment = AlignH::Center;
            text->Position = Vector2(0, DIALOG_PADDING + CONTROL_HEIGHT * 2);
            text->TextAlignment = AlignH::Center;
            text->Color = DIALOG_TITLE_COLOR;

            text = AddChild<Label>("contents were incinerated", FontSize::MediumBlue);
            text->HorizontalAlignment = AlignH::Center;
            text->Position = Vector2(0, DIALOG_PADDING + CONTROL_HEIGHT * 3);
            text->TextAlignment = AlignH::Center;
            text->Color = DIALOG_TITLE_COLOR;

            Size.x = _titleSize.x + DIALOG_PADDING * 4 + 20;
            Size.y = CONTROL_HEIGHT * 6 + DIALOG_PADDING * 2;

            //if (missionFailed) {
            //    Size.y += CONTROL_HEIGHT * 2;
            //    text = AddChild<Label>("mission failed", FontSize::Medium);
            //    text->HorizontalAlignment = AlignH::Center;
            //    text->Position = Vector2(0, DIALOG_PADDING + CONTROL_HEIGHT * 5);
            //    text->TextAlignment = AlignH::Center;
            //    text->Color = INSANE_TEXT;
            //}

            auto okay = AddChild<Button>(missionFailed ? "mission failed" : "rest in peace");
            okay->VerticalAlignment = AlignV::Bottom;
            okay->HorizontalAlignment = AlignH::Center;
            okay->Position = Vector2(0, -DIALOG_PADDING);
            okay->ClickAction = [this] { State = CloseState::Accept; };
            okay->FocusColor = missionFailed ? INSANE_TEXT_FOCUSED : FOCUS_COLOR;
        }

        bool OnTryClose() override {
            return true; // Allow closing this dialog with escape
        }

        void OnClose() override {
            Game::SetState(GameState::ScoreScreen);
        }

        void OnDraw() override {
            DialogBase::OnDraw();

            // White Background
            Render::CanvasBitmapInfo cbi;
            cbi.Size = Render::UICanvas->GetSize();
            cbi.Texture = Render::Materials->White().Handle();
            cbi.Color = Color(3, 3, 3, 1);
            Render::UICanvas->DrawBitmap(cbi, 0); // Draw on layer 0
        }
    };


    List<MissionInfo> GetDescent1MissionList() {
        List<MissionInfo> missions = Resources::ReadMissionDirectory("d1/missions");

        if (Resources::FoundDescent1()) {
            SPDLOG_INFO("Adding retail D1 to mission list");
            missions.insert(missions.begin(), Game::CreateDescent1Mission(false));
        }
        else if (Resources::FoundDescent1Demo()) {
            SPDLOG_INFO("Adding D1 demo to mission list");
            missions.insert(missions.begin(), Game::CreateDescent1Mission(true));
        }

        return missions;
    }

    void ShowDifficultySelect(MissionInfo& mission, int& level, DifficultyLevel& difficulty) {
        auto screen = ShowScreen(make_unique<DifficultyDialog>(difficulty));

        screen->CloseCallback = [&mission, difficulty, level](CloseState state) {
            if (state == CloseState::Accept) {
                Game::StartMission();
                Game::Difficulty = difficulty;
                Game::LoadLevelFromMission(mission, level);
            }
        };
    }

    void ShowLevelSelect(MissionInfo& mission, int& level, DifficultyLevel& difficulty) {
        auto screen = ShowScreenT(make_unique<LevelSelectDialog>((int)mission.Levels.size(), level));

        screen->CloseCallback = [&mission, &difficulty, &level](CloseState state) {
            if (state == CloseState::Accept)
                ShowDifficultySelect(mission, level, difficulty);
        };
    }

    class PlayD1Dialog : public DialogBase {
        List<MissionInfo> _missions;
        DifficultyLevel _difficulty{};
        int _level = 1;
        MissionInfo* _mission = nullptr;

    public:
        PlayD1Dialog(const List<MissionInfo>& missions) : _missions(missions) {
            Size = Vector2(500, 460);
            CloseOnConfirm = false;
            _difficulty = Game::Difficulty;

            auto title = make_unique<Label>("select mission", FontSize::MediumBlue);
            title->VerticalAlignment = AlignV::Top;
            title->HorizontalAlignment = AlignH::Center;
            title->Position = Vector2(0, DIALOG_PADDING);
            title->Color = DIALOG_TITLE_COLOR;
            AddChild(std::move(title));

            auto missionList = AddChild<ListBox>(14);

            // Add missions
            for (auto& mission : _missions) {
                missionList->Items.push_back(mission.Name);
            }

            // Then select and scroll to recent one
            for (int i = 0; i < _missions.size(); i++) {
                if (_missions[i].Path == Settings::Inferno.RecentMission) {
                    missionList->SetIndex(i);
                    missionList->ScrollItemToTop(i);
                }
            }

            missionList->ClickItemAction = [this](int index) {
                if (auto mission = Seq::tryItem(_missions, index)) {
                    SPDLOG_INFO("Mission: {}", mission->Path.string());
                    Settings::Inferno.RecentMission = mission->Path.string();
                    _mission = mission;
                    _level = 1;

                    if (mission->Levels.size() > 1) {
                        ShowLevelSelect(*mission, _level, _difficulty);
                    }
                    else {
                        ShowDifficultySelect(*mission, _level, _difficulty); // Use the first level instead of showing selection screen
                    }

                    Sound::Play2D(SoundResource{ ActionSound });
                }
            };

            missionList->Position = Vector2(30, 60);
            missionList->Size.x = 425;
            missionList->Padding = Vector2(10, 5);
        }
    };

    class MainMenu : public ScreenBase {
        DifficultyLevel _difficulty{};
        int _level = 1;
        MissionInfo _mission;

    public:
        MainMenu() {
            CloseOnConfirm = false;

            auto panel = AddChild<StackPanel>();
            panel->Position = Vector2(50, 180);
            //panel->Position = Vector2(45, 140);
            panel->HorizontalAlignment = AlignH::CenterRight;
            panel->VerticalAlignment = AlignV::Top;

            panel->AddChild<Button>("Play Descent", [this] {
                if (Game::DemoMode) {
                    _mission = Game::CreateDescent1Mission(true);
                    ShowLevelSelect(_mission, _level, _difficulty); // Demo has 7 levels
                }
                else {
                    auto missions = GetDescent1MissionList();

                    if (missions.size() == 1) {
                        _mission = missions[0];
                        if (_mission.Levels.size() > 1) {
                            ShowLevelSelect(_mission, _level, _difficulty);
                        }
                        else {
                            _level = 1;
                            ShowDifficultySelect(_mission, _level, _difficulty); // Use the first level instead of showing selection screen
                        }
                    }
                    else {
                        ShowScreen(make_unique<PlayD1Dialog>(missions));
                    }
                }
            });

            //panel->AddChild<Button>("Play Descent 2");
            panel->AddChild<Button>("Load Game", [] {
                ShowScreen(make_unique<LoadDialog>());
            });
            panel->AddChild<Button>("Options", [] {
                ShowScreen(make_unique<OptionsMenu>());
            });
            //panel->AddChild<Button>("High Scores", [] {
            //    Game::SetState(GameState::ScoreScreen);
            //});
            //panel->AddChild<Button>("Credits");
            panel->AddChild<Button>("Level Editor", [] {
                Game::SetState(GameState::Editor);
            });
            panel->AddChild<Button>("Quit", [] {
                std::array messages = {
                    "dravis has a mission for you",
                    "quitting means you won't\ncollect that paycheck",
                    "PTMC needs you\nmaterial defender",
                    "another class 1 driller\nneeds dismantling",
                    "I promise the next\nlevel has fewer drillers",
                    "Are you sure?\nJosh will miss you",
                    //"Guide-bot is coming back to get you"
                };

                auto random = RandomInt((int)messages.size() - 1);
                auto confirmDialog = make_unique<ConfirmDialog>(messages[random], "resign", "enlist");
                confirmDialog->ActionSound = ""; // clear the sound because quitting interrupts it
                confirmDialog->CloseCallback = [](CloseState state) {
                    if (state == CloseState::Accept)
                        Shell::Quit();
                };

                ShowScreen(std::move(confirmDialog));
            });
        }

        void OnDraw() override {
            ScreenBase::OnDraw();

            float titleX = 167;
            float titleY = 50;
            float titleScale = 1.25f;
            float anim = (((float)sin(Clock.GetTotalTimeSeconds()) + 1) * 0.5f * 0.25f) + 0.6f;
            auto titleColor = Color(1, .5f, .2f) * abs(anim) * 4;

            //auto logoHeight = MeasureString("inferno", FontSize::Big).y * titleScale;
            {
                Render::DrawTextInfo dti;
                dti.Font = FontSize::Big;
                dti.HorizontalAlign = AlignH::Center;
                dti.VerticalAlign = AlignV::Top;
                dti.Position = Vector2(titleX, titleY + 45);
                dti.Color = titleColor;
                dti.Scale = 0.75f;
                Render::UICanvas->DrawText(Game::DemoMode ? "demo" : "beta", dti);
            }
            //dti.Color = Color(1, 0.7f, 0.54f);

            //Render::UICanvas->DrawGameText("descent remastered", dti);
            //dti.Position.y += 15;
            //Render::UICanvas->DrawGameText("descent II", dti);
            //dti.Position.y += 15;
            //Render::UICanvas->DrawGameText("descent 3 enhancements enabled", dti);

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

                dti.Color = titleColor;
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
                Render::UICanvas->DrawText("portions (c) parallax", dti);
            }
        }
    };

    //void DrawTestText(const Vector2& position, FontSize font, uchar lineLen = 32) {
    //    Render::DrawTextInfo dti;
    //    dti.Font = font;

    //    auto drawRange = [&](uchar min, uchar max, float yOffset) {
    //        string text;
    //        for (uchar i = min; i < max; i++)
    //            text += i;

    //        dti.Position = position;
    //        dti.Position.y += yOffset;
    //        Render::HudCanvas->DrawText(text, dti);
    //    };

    //    auto lineHeight = MeasureString("M", font).y;

    //    for (uchar i = 0; i < uchar(255) / lineLen; i++) {
    //        drawRange(i * lineLen, (i + 1) * lineLen, float(i * (lineHeight + 2)));
    //    }
    //}

    class PauseMenu : public DialogBase {
        float _topOffset = 150;
        Vector2 _menuSize;

    public:
        PauseMenu() : DialogBase("", false) {
            CloseOnConfirm = false;
            ActionSound = ""; // Clear pause sound because buttons already have one

            auto panel = make_unique<StackPanel>();
            panel->Position = Vector2(0, _topOffset);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;

            panel->AddChild<Button>("Resume", [] {
                Game::SetState(GameState::Game);
            }, AlignH::Center);
            //panel->AddChild<Button>("Save Game", AlignH::Center);

            panel->AddChild<Button>("Restart", [this] {
                auto confirmDialog = make_unique<ConfirmDialog>("Restart the level?");
                confirmDialog->CloseCallback = [this](CloseState state) {
                    if (state == CloseState::Accept)
                        Game::RestartLevel();
                };
                ShowScreen(std::move(confirmDialog));
            }, AlignH::Center);

            panel->AddChild<Button>("Load Game", [] {
                ShowScreen(make_unique<LoadDialog>());
            }, AlignH::Center);

            panel->AddChild<Button>("Options", [] {
                ShowScreen(make_unique<OptionsMenu>());
            }, AlignH::Center);
            panel->AddChild<Button>("Quit", [this] {
                auto confirmDialog = make_unique<ConfirmDialog>("abort mission?");
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
                cbi.Size = ScreenSize;
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
        Screens.clear();
        ShowScreen(make_unique<MainMenu>());
        ShowScreen(make_unique<SensitivityDialog>());
        //ShowScreen(make_unique<ScoreScreen>(ScoreInfo{ .ExtraLives = 1 }, true));
    }

    void ShowFailedEscapeDialog(bool missionFailed) {
        Screens.clear();
        Game::ScreenGlow.SetTarget(Color(0, 0, 0, 0), Game::Time, 0);
        ShowScreen(make_unique<FailedEscapeDialog>(missionFailed));
    }

    void ShowPauseDialog() {
        Screens.clear();
        ShowScreen(make_unique<PauseMenu>());
    }

    void ShowScoreScreen(const ScoreInfo& score, bool secretLevel) {
        Screens.clear();

        auto textures = std::to_array<const string>({ "menu-bg" });
        Graphics::LoadTextures(textures);

        ShowScreen(make_unique<ScoreScreen>(score, secretLevel));
    }

    void Update() {
        //DrawTestText({ 10, 0 }, FontSize::Medium);
        //DrawTestText({ 10, 150 }, FontSize::Small);
        //DrawTestText({ 10, 170 }, FontSize::Big, 24);

        if (Screens.empty()) return;

        for (size_t i = 0; i < Screens.size(); i++) {
            auto& screen = Screens[i];

            if (i == Screens.size() - 1) {
                bool inputCaptured = InputCaptured; // store the capture state so cancelling doesn't exit the screen in the same frame

                screen->OnUpdate(); // only update input for topmost screen

                if (Input::MouseButtonPressed(Input::MouseButtons::LeftClick))
                    screen->OnMouseClick(Input::MousePosition);

                if (!inputCaptured && Input::MenuActions.HasAction()) {
                    screen->OnMenuAction(Input::MenuActions);
                }
            }

            screen->OnUpdateLayout();
            screen->OnDraw();
        }

        if (Screens.back()->State != CloseState::None) {
            CloseScreen();

            //CloseScreen();
            //string sound = Screens.back()->ActionSound;
            //if (CloseScreen())
            //    Sound::Play2D(SoundResource{ MENU_SELECT_SOUND });
            //Sound::Play2D(SoundResource{ sound });
        }
        //else if (Screens.back()->State == CloseState::Cancel) {
        //    if (CloseScreen())
        //        Sound::Play2D(SoundResource{ MENU_BACK_SOUND });
        //}

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
