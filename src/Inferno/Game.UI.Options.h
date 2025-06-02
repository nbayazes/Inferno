#pragma once
#include "Game.UI.Controls.h"
#include "Graphics.h"
#include "Procedural.h"
#include "Resources.h"
#include "Game.UI.Bindings.h"

namespace Inferno::UI {
    ScreenBase* ShowScreen(Ptr<ScreenBase> screen);

    class SoundMenu : public DialogBase {
        List<AudioEngine::RendererDetail> _devices;
        int _deviceIndex = 0;
        int _musicIndexD1 = 0;
        int _musicIndexD2 = 1;

    public:
        SoundMenu() : DialogBase("Sound Options") {
            CloseOnConfirm = false;
            Size = Vector2(620, 460);

            auto bombSound = Seq::tryItem(Inferno::Resources::Descent1.Sounds, (int)SoundID::DropBomb);
            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_HEADER_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;
            panel->Spacing = 2;

            auto volume = make_unique<SliderFloat>("Master Volume", 0.0f, 1.0f, Settings::Inferno.MasterVolume);
            volume->LabelWidth = 250;
            volume->ShowValue = false;
            volume->ChangeSound.D1 = bombSound ? *bombSound : -1;
            volume->OnChange = [](float value) { Sound::SetMasterVolume(value); };

            panel->AddChild(std::move(volume));

            auto fxVolume = make_unique<SliderFloat>("FX Volume", 0.0f, 1.0f, Settings::Inferno.EffectVolume);
            fxVolume->LabelWidth = 250;
            fxVolume->ShowValue = false;
            fxVolume->ChangeSound.D1 = bombSound ? *bombSound : -1;
            fxVolume->OnChange = [](float value) { Sound::SetEffectVolume(value); };
            panel->AddChild(std::move(fxVolume));

            auto music = make_unique<SliderFloat>("Music Volume", 0.0f, 1.0f, Settings::Inferno.MusicVolume);
            music->LabelWidth = 250;
            music->ShowValue = false;
            music->OnChange = [](float value) { Sound::SetMusicVolume(value); };
            panel->AddChild(std::move(music));

            // Sound device selector
            try {
                List<string> deviceNames;
                _devices.push_back({ L"", L"Default" });

                for (auto& device : AudioEngine::GetRendererDetails()) {
                    _devices.push_back(device);
                }

                for (auto& device : _devices) {
                    deviceNames.push_back(Narrow(device.description));
                }

                auto device = ComboSelect::Create("Sound Device", deviceNames, _deviceIndex);
                device->MenuActionSound = ""; // Clear action sound, as switching sound devices orphans the one-shot effect which results in a warning
                device->LabelWidth = 250;
                device->OnChange = [this](int value) {
                    auto deviceId = &_devices[value].deviceId;
                    Sound::Init(Shell::Hwnd, deviceId->empty() ? nullptr : deviceId);
                    Game::PlayMainMenuMusic();
                };
                panel->AddChild(std::move(device));
            }
            catch (...) {
                SPDLOG_ERROR("Error getting sound devices");
            }

            //auto d1Music = OptionSpinner::Create("D1 Music", { "Mission", "Jukebox" }, _musicIndexD1);
            //d1Music->LabelWidth = 250;
            //panel->AddChild(std::move(d1Music));

            //auto d2Music = OptionSpinner::Create("D2 Music", { "Mission", "Jukebox" }, _musicIndexD2);
            //d2Music->LabelWidth = 250;
            //panel->AddChild(std::move(d2Music));

            //panel->AddChild<Checkbox>("SFX Occlusion", Settings::Inferno.UseSoundOcclusion);

            AddChild(std::move(panel));
        }
    };

    class SensitivityDialog : public DialogBase {
        float _secondaryPosition = 240;
        List<Input::InputDevice> _devices;
        ComboSelect* _deviceList = nullptr;
        StackPanel* _stack = nullptr;
        gsl::strict_not_null<int*> _index;

    public:
        SensitivityDialog(int& deviceIndex) : DialogBase("sensitivity and deadzone"), _index(&deviceIndex) {
            Size = Vector2(620, 460);

            auto deviceNames = GetDeviceNames();
            if (*_index > deviceNames.size() - 1) *_index = 0;

            _deviceList = AddChild<ComboSelect>("Input Device", deviceNames, *_index);
            _deviceList->LabelWidth = 225;
            _deviceList->Size = Vector2(Size.x - DIALOG_PADDING * 2, CONTROL_HEIGHT);

            _deviceList->Position = Vector2(DIALOG_PADDING, DIALOG_HEADER_PADDING);
            _deviceList->OnChange = [this](int /*index*/) {
                UpdateList();
            };

            _stack = AddChild<StackPanel>();
            _stack->Position = Vector2(DIALOG_PADDING, DIALOG_HEADER_PADDING + CONTROL_HEIGHT + 8);
            _stack->Size.x = Size.x - DIALOG_PADDING * 3;

            UpdateList();
        }

    private:
        void UpdateList() {
            _stack->Children.clear();

            const auto addSlider = [this](string_view name, float min, float max, float& value) {
                auto slider = _stack->AddChild<SliderFloat>(name, min, max, value, 2);
                slider->LabelWidth = 150;
                slider->ShowValue = true;
                slider->ValueWidth = 60;
            };

            if (*_index == 0) {
                // keyboard
                auto& bindings = Game::Bindings.GetKeyboard();
                auto& rotation = bindings.sensitivity.rotation;
                addSlider("Pitch", 0.0f, 1.0f, rotation.x);
                addSlider("Yaw", 0.0f, 1.0f, rotation.y);
                addSlider("Roll", 0.0f, 1.0f, rotation.z);
            }
            else if (*_index == 1) {
                // mouse
                auto& bindings = Game::Bindings.GetMouse();
                auto& rotation = bindings.sensitivity.rotation;
                addSlider("Pitch", 0.0f, 3.0f, rotation.x);
                addSlider("Yaw", 0.0f, 3.0f, rotation.y);
                addSlider("Roll", 0.0f, 3.0f, rotation.z);

                _stack->AddChild<Label>("");
                _stack->AddChild<Label>("automap", FontSize::MediumBlue);
                auto& automap = bindings.sensitivity.automap;
                addSlider("pitch", 0.0f, 2.0f, automap.x);
                addSlider("yaw", 0.0f, 2.0f, automap.y);
                //addSlider("roll", 0.0f, 2.0f, automap.z);
            }
            else {
                // input device

                if (auto device = Seq::tryItem(_devices, *_index - 2)) {
                    if (auto bindings = Game::Bindings.GetDevice(device->guid)) {
                        auto& rotation = bindings->sensitivity.rotation;
                        addSlider("Pitch", 0.0f, 2.0f, rotation.x);
                        addSlider("Yaw", 0.0f, 2.0f, rotation.y);
                        addSlider("Roll", 0.0f, 2.0f, rotation.z);

                        auto& thrust = bindings->sensitivity.thrust;
                        addSlider("fwd/Rev", 0.0f, 2.0f, thrust.z);
                        addSlider("Slide L/R", 0.0f, 2.0f, thrust.x);
                        addSlider("Slide U/D", 0.0f, 2.0f, thrust.y);

                        _stack->AddChild<Label>("");
                        auto label = _stack->AddChild<Label>("Deadzone");
                        label->Color = GREY_TEXT;
                        auto& rotationdz = bindings->sensitivity.rotationDeadzone;
                        addSlider("Pitch", 0.0f, 1.0f, rotationdz.x);
                        addSlider("Yaw", 0.0f, 1.0f, rotationdz.y);
                        addSlider("Roll", 0.0f, 1.0f, rotationdz.z);

                        auto& thrustdz = bindings->sensitivity.thrustDeadzone;
                        addSlider("fwd/Rev", 0.0f, 1.0f, thrustdz.z);
                        addSlider("Slide L/R", 0.0f, 1.0f, thrustdz.x);
                        addSlider("Slide U/D", 0.0f, 1.0f, thrustdz.y);
                    }
                }
            }

            // fix flicker when adding controls
            OnUpdateLayout();
        }

        List<string> GetDeviceNames() {
            List<string> deviceNames = { "Keyboard", "Mouse" };

            _devices = Input::GetDevices(); // Copy the current devices

            for (auto& device : _devices) {
                deviceNames.push_back(device.name);
            }

            return deviceNames;
        }
    };

    class InputMenu : public DialogBase {
        // the selected control. 0 is keyboard, 1 is mouse, 1 > is controllers and joysticks.
        // located here so the selection stays in sync between bindings and sensitivity.
        // Static so that selection is remembered between opening the menu
        static inline int _deviceIndex = 0;  
    public:
        InputMenu() : DialogBase("Input Options") {
            Size = Vector2(620, 460);
            CloseOnConfirm = false;

            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_HEADER_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;

            panel->AddChild<Checkbox>("Enable Mouse", Settings::Inferno.EnableMouse);
            panel->AddChild<Checkbox>("Enable joystick", Settings::Inferno.EnableJoystick);
            panel->AddChild<Checkbox>("Enable gamepad", Settings::Inferno.EnableGamepad);
            panel->AddChild<Checkbox>("Classic pitch speed", Settings::Inferno.HalvePitchSpeed);
            panel->AddChild<Checkbox>("Use mouselook [cheat]", Settings::Inferno.UseMouselook);

            panel->AddChild<Label>("");
            panel->AddChild<Button>("Customize bindings", [this] {
                ShowScreen(make_unique<BindingDialog>(_deviceIndex));
            });
            panel->AddChild<Button>("sensitivity and deadzone", [this] {
                ShowScreen(make_unique<SensitivityDialog>(_deviceIndex));
            });

            panel->AddChild<Label>("");
            auto controlMode = panel->AddChild<OptionSpinner>("automap mode", std::initializer_list<string_view>{ "Mouselook", "Orbit" }, (int&)Settings::Inferno.AutomapMode);
            controlMode->LabelWidth = 250;
            panel->AddChild<Checkbox>("Invert automap X", Settings::Inferno.AutomapInvertX);
            panel->AddChild<Checkbox>("Invert automap Y", Settings::Inferno.AutomapInvertY);

            AddChild(std::move(panel));
        }
    };

    class GraphicsMenu : public DialogBase {
        int _msaaSamples = 0;
        bool _useVsync;

    public:
        GraphicsMenu() : DialogBase("Graphics Options") {
            Size = Vector2(620, 460);
            CloseOnConfirm = false;

            auto panel = AddChild<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_HEADER_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;
            panel->Spacing = 2;

            panel->AddChild<Checkbox>("Fullscreen", Settings::Inferno.Fullscreen);

            _useVsync = Settings::Graphics.UseVsync;
            panel->AddChild<Checkbox>("VSync", _useVsync);
            panel->AddChild<Checkbox>("Procedural textures", Settings::Graphics.EnableProcedurals);

            auto renderScale = panel->AddChild<SliderFloat>("Render scale", 0.05f, 1.0f, Settings::Graphics.RenderScale, 2);
            //renderScale->LabelWidth = 300;
            renderScale->ShowValue = true;
            renderScale->LabelWidth = 250;
            renderScale->ValueWidth = 50;
            renderScale->Step = 0.05f;
            renderScale->BigStep = 0.25f;

            auto brightness = panel->AddChild<SliderFloat>("Brightness", 0.5f, 1.5f, Inferno::Settings::Graphics.Brightness, 2);
            brightness->ShowValue = true;
            brightness->Snap = true;
            brightness->LabelWidth = 250;
            brightness->ValueWidth = 50;

            auto fov = panel->AddChild<SliderFloat>("Field of view", 60.0f, 90.0f, Inferno::Settings::Graphics.FieldOfView, 0);
            fov->ShowValue = true;
            fov->LabelWidth = 250;
            fov->ValueWidth = 50;
            fov->Step = 1.0;
            fov->BigStep = 5.0f;

            auto upscaleFilter = panel->AddChild<OptionSpinner>("upscale filtering", std::initializer_list<string_view>{ "Sharp", "Smooth" }, (int&)Settings::Graphics.UpscaleFilter);
            upscaleFilter->LabelWidth = 340;

            auto filterMode = panel->AddChild<OptionSpinner>("Texture Filtering", std::initializer_list<string_view>{ "None", "Enhanced", "Smooth" }, (int&)Settings::Graphics.FilterMode);
            filterMode->LabelWidth = 340;

            _msaaSamples = [] {
                switch (Settings::Graphics.MsaaSamples) {
                    default:
                    case 1: return 0;
                    case 2: return 1;
                    case 4: return 2;
                    case 8: return 3;
                }
            }();

            auto msaa = panel->AddChild<OptionSpinner>("MSAA", std::initializer_list<string_view>{ "None", "2x", "4x", "8x" }, _msaaSamples);
            msaa->LabelWidth = 340;

            panel->AddChild<Label>("");
            panel->AddChild<Label>("Framerate limits", FontSize::MediumBlue);
            panel->AddChild<Checkbox>("Enable Foreground Limit", Inferno::Settings::Graphics.EnableForegroundFpsLimit);

            auto foreground = panel->AddChild<Slider>("Foreground", 30, 360, Inferno::Settings::Graphics.ForegroundFpsLimit);
            foreground->ShowValue = true;
            foreground->LabelWidth = 200;
            foreground->ValueWidth = 40;

            auto background = panel->AddChild<Slider>("Background", 10, 60, Inferno::Settings::Graphics.BackgroundFpsLimit);
            background->ShowValue = true;
            background->LabelWidth = 200;
            background->ValueWidth = 40;
        }

        void OnClose() override {
            auto msaaSamples = [this] {
                switch (_msaaSamples) {
                    default:
                    case 0: return 1;
                    case 1: return 2;
                    case 2: return 4;
                    case 3: return 8;
                }
            }();

            if (msaaSamples != Settings::Graphics.MsaaSamples) {
                Settings::Graphics.MsaaSamples = msaaSamples;
                Graphics::ReloadResources();
            }

            if (_useVsync != Settings::Graphics.UseVsync) {
                Settings::Graphics.UseVsync = _useVsync;
                // Recreate the swap chain if vsync changes
                Graphics::CreateWindowSizeDependentResources(true);
            }
        }
    };

    constexpr auto DEFAULT_PRIMARY_NAMES = std::to_array<string_view>({
        "laser cannon",
        "vulcan cannon",
        "spreadfire cannon",
        "plasma cannon",
        "fusion cannon",
        "super laser cannon",
        "gauss cannon",
        "helix cannon",
        "phoenix cannon",
        "omega cannon",
        "quad laser cannon",
        "quad super laser cannon"
    });

    constexpr auto DEFAULT_SECONDARY_NAMES = std::to_array<string_view>({
        "concussion missile",
        "homing missile",
        "proximity bomb",
        "smart missile",
        "mega missile",
        "flash missile",
        "guided missile",
        "smart mine",
        "mercury missile",
        "earthshaker missile"
    });

    constexpr auto NO_AUTOSELECT = "--- NEVER AUTOSELECT BELOW ---";

    class WeaponPriorityList : public ControlBase {
        span<uint8> _priority;
        span<const string_view> _labels;
        int _selection = 0;
        int _startSelection = 0;

        bool _editing = false;
        float _rowHeight = 15;
        FontSize _font;

    public:
        WeaponPriorityList(span<uint8> priority, const span<const string_view> labels, float rowHeight, FontSize font = FontSize::Small)
            : _priority(priority), _labels(labels), _rowHeight(rowHeight), _font(font) {
            Size.y = _rowHeight * priority.size();
        }

        void OnDraw() override {
            const float scale = GetScale();

            for (size_t i = 0; i < _priority.size(); i++) {
                auto priority = _priority[i];
                if (priority != 255 && priority >= _labels.size()) continue;
                auto label = priority == 255 ? NO_AUTOSELECT : _labels[priority];

                Render::DrawTextInfo dti;
                dti.Font = _font;
                dti.Color = _selection == i ? GOLD_TEXT : GREY_TEXT;

                if (_editing && _startSelection == i) {
                    dti.Color = GOLD_TEXT_GLOW;
                }

                dti.Position.x = ScreenPosition.x + Padding.x * scale;
                dti.Position.y = ScreenPosition.y + _rowHeight * i * scale;
                Render::UICanvas->DrawRaw(label, dti, Layer + 1);

                // Debugging
                //Render::CanvasBitmapInfo cbi;
                //cbi.Position = Vector2{ ScreenPosition.x + Padding.x * GetScale(), ScreenPosition.y + RowHeight * i * GetScale() };
                //cbi.Size = Vector2{ ScreenSize.x, RowHeight * GetScale() };
                //cbi.Texture = Render::Materials->White().Handle();
                //cbi.Color = Color(0, 1, 0, 1);
                //Render::UICanvas->DrawBitmap(cbi, Layer);
            }

            {
                // Background
                Render::CanvasBitmapInfo cbi;
                cbi.Position = ScreenPosition - Vector2(5, 5) * scale;
                cbi.Size = ScreenSize + Vector2(10, 10) * scale;
                cbi.Texture = Render::Materials->White().Handle();
                cbi.Color = Color(0, 0, 0, 1);
                Render::UICanvas->DrawBitmap(cbi, Layer);
            }
        }

        void OnUpdate() override {
            if (Input::MouseMoved()) {
                for (size_t i = 0; i < _priority.size(); i++) {
                    Vector2 position = { ScreenPosition.x + Padding.x * GetScale(), ScreenPosition.y + _rowHeight * i * GetScale() };
                    if (RectangleContains(position, { ScreenSize.x, _rowHeight * GetScale() }, Input::MousePosition)) {
                        _selection = (int)i;
                    }
                }
            }

            if (Input::MouseButtonPressed(Input::MouseButtons::LeftClick) && Focused) {
                if (RectangleContains(ScreenPosition, ScreenSize, Input::MousePosition)) {
                    if (_editing) {
                        std::swap(_priority[_startSelection], _priority[_selection]);
                        _editing = false;
                    }
                    else {
                        _editing = true;
                    }

                    _startSelection = _selection;
                    Sound::Play2D({ MENU_SELECT_SOUND });
                }
            }
        }

        bool OnMenuAction(Input::MenuActionState action) override {
            if (action == MenuAction::Up) {
                _selection--;
                if (_selection < 0) _selection = (int)_priority.size() - 1;

                if (_editing) {
                    std::swap(_priority[_startSelection], _priority[_selection]);
                }

                _startSelection = _selection;
                return true;
            }

            if (action == MenuAction::Down) {
                _selection++;
                if (_selection >= (int)_priority.size()) _selection = 0;

                if (_editing) {
                    std::swap(_priority[_startSelection], _priority[_selection]);
                }

                _startSelection = _selection;
                return true;
            }

            if (action == MenuAction::Confirm) {
                _editing = !_editing;
                Sound::Play2D({ MENU_SELECT_SOUND });
                _startSelection = _selection;
                return true;
            }

            return false;
        }
    };

    class PriorityMenu : public DialogBase {
        float _secondaryPosition = 240;

    public:
        PriorityMenu(string_view title, span<uint8> priorityList, span<const string_view> labels) : DialogBase(title) {
            float rowHeight = 15;
            Size = Vector2(400, DIALOG_HEADER_PADDING + rowHeight * priorityList.size() + DIALOG_PADDING + 10);

            auto primaries = AddChild<WeaponPriorityList>(priorityList, labels, rowHeight);
            primaries->Size.x = Size.x - DIALOG_PADDING * 4;
            primaries->Position = Vector2(DIALOG_PADDING * 2, DIALOG_HEADER_PADDING + 10);
        }

        //void OnDraw() override {
        //    DialogBase::OnDraw();

        //    //auto scale = GetScale();

        //    //{
        //    //    // Background
        //    //    Render::CanvasBitmapInfo cbi;
        //    //    //cbi.Position = ScreenPosition;
        //    //    //cbi.Size = ScreenSize;
        //    //    cbi.Position = ScreenPosition + Vector2(DIALOG_PADDING, DIALOG_HEADER_PADDING + 10 - 4) * scale;
        //    //    cbi.Size.x = ScreenSize.x - DIALOG_PADDING * 2 * scale;
        //    //    cbi.Size.y = 15 * 13 * scale + 8 * scale;
        //    //    cbi.Texture = Render::Materials->White().Handle();
        //    //    cbi.Color = Color(0, 0, 0, 1);
        //    //    Render::UICanvas->DrawBitmap(cbi, Layer);
        //    //}
        //}
    };

    class GameOptionsMenu : public DialogBase {
    public:
        GameOptionsMenu() : DialogBase("Game Options") {
            Size = Vector2(620, 460);
            CloseOnConfirm = false;

            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_HEADER_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;
            panel->Spacing = 2;

            auto wiggle = make_unique<OptionSpinner>("Ship Wiggle", std::initializer_list<string_view>{ "Normal", "Reduced", "None" }, (int&)Settings::Inferno.ShipWiggle);
            wiggle->LabelWidth = 320;
            panel->AddChild(std::move(wiggle));

            auto roll = make_unique<OptionSpinner>("Ship Roll", std::initializer_list<string_view>{ "Normal", "Reduced" }, (int&)Settings::Inferno.ShipRoll);
            roll->LabelWidth = 320;
            panel->AddChild(std::move(roll));

            //panel->AddChild<Checkbox>("ship auto-leveling", Settings::Inferno.ShipAutolevel);

            //panel->AddChild<Checkbox>("only cycle autoselect weapons", Settings::Inferno.OnlyCycleAutoselectWeapons);
            //panel->AddChild<Checkbox>("sticky rearview", Settings::Inferno.StickyRearview);
            panel->AddChild<Checkbox>("charging fusion slows time", Settings::Inferno.SlowmoFusion);
            panel->AddChild<Checkbox>("hud dirt and glare", Settings::Inferno.HudGlare);
            //panel->AddChild<Checkbox>("prefer high res fonts", Settings::Inferno.PreferHighResFonts);
            panel->AddChild<Checkbox>("only autoselect when empty", Settings::Inferno.OnlyAutoselectWhenEmpty);
            panel->AddChild<Checkbox>("no weapon autoselect while firing", Settings::Inferno.NoAutoselectWhileFiring);

            panel->AddChild<Label>("");

            panel->AddChild<Button>("Primary Weapon priority", [] {
                ShowScreen(make_unique<PriorityMenu>("Primary Priority", Settings::Inferno.PrimaryPriority, DEFAULT_PRIMARY_NAMES));
            });

            panel->AddChild<Button>("Secondary Weapon priority", [] {
                ShowScreen(make_unique<PriorityMenu>("Secondary Priority", Settings::Inferno.SecondaryPriority, DEFAULT_SECONDARY_NAMES));
            });

            panel->AddChild<Label>("");
            panel->AddChild<Checkbox>("developer hotkeys", Settings::Inferno.EnableDevHotkeys);

            AddChild(std::move(panel));
        }
    };

    class OptionsMenu : public DialogBase {
    public:
        OptionsMenu() : DialogBase("Options", false) {
            Size = Vector2(200, 30 * 4 + DIALOG_HEADER_PADDING + DIALOG_PADDING);
            CloseOnConfirm = false;
            CloseOnClickOutside = true;

            //TitleAlignment = AlignH::Left;

            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_HEADER_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;
            panel->Spacing = 2;

            //Size.x = MeasureString("Graphics", FontSize::Medium).x + DIALOG_PADDING * 2;
            //Size.y += DIALOG_PADDING;

            panel->AddChild<Button>("Graphics", [] { ShowScreen(make_unique<GraphicsMenu>()); }, AlignH::Center);
            panel->AddChild<Button>("Sound", [] { ShowScreen(make_unique<SoundMenu>()); }, AlignH::Center);
            panel->AddChild<Button>("Input", [] { ShowScreen(make_unique<InputMenu>()); }, AlignH::Center);
            panel->AddChild<Button>("Game", [] { ShowScreen(make_unique<GameOptionsMenu>()); }, AlignH::Center);

            AddChild(std::move(panel));
        }

        void OnClose() override {
            Settings::Save();
        }
    };
}
