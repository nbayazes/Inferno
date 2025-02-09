﻿#pragma once
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

            auto bombSound = Seq::tryItem(Inferno::Resources::GameDataD1.Sounds, (int)SoundID::DropBomb);
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

            auto d1Music = OptionSpinner::Create("D1 Music", { "Mission", "Jukebox" }, _musicIndexD1);
            d1Music->LabelWidth = 250;
            panel->AddChild(std::move(d1Music));

            auto d2Music = OptionSpinner::Create("D2 Music", { "Mission", "Jukebox" }, _musicIndexD2);
            d2Music->LabelWidth = 250;
            panel->AddChild(std::move(d2Music));

            //panel->AddChild<Checkbox>("SFX Occlusion", Settings::Inferno.UseSoundOcclusion);

            AddChild(std::move(panel));
        }
    };

    class InputMenu : public DialogBase {
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
            panel->AddChild<Label>("");
            panel->AddChild<Checkbox>("Classic pitch speed", Settings::Inferno.HalvePitchSpeed);

            {
                panel->AddChild<Label>("");
                panel->AddChild<Label>("Mouse Settings", FontSize::MediumBlue);

                //_value4 = (int)std::floor(Settings::Inferno.MouseSensitivity * 1000);
                auto mouseXAxis = make_unique<SliderFloat>("X-Axis", 0.001f, 0.050f, Settings::Inferno.MouseSensitivity);
                mouseXAxis->LabelWidth = 100;
                mouseXAxis->ShowValue = true;
                mouseXAxis->ValueWidth = 60;
                panel->AddChild(std::move(mouseXAxis));

                auto mouseYAxis = make_unique<SliderFloat>("Y-Axis", 0.001f, 0.050f, Settings::Inferno.MouseSensitivityX);
                mouseYAxis->LabelWidth = 100;
                mouseYAxis->ShowValue = true;
                mouseYAxis->ValueWidth = 60;
                panel->AddChild(std::move(mouseYAxis));
            }

            panel->AddChild<Checkbox>("Invert Y-axis", Settings::Inferno.InvertY);

            panel->AddChild<Label>("");
            panel->AddChild<Button>("Customize bindings", [] {
                ShowScreen(make_unique<BindingDialog>());
            });
            panel->AddChild<Button>("sensitivity");

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

            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_HEADER_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;
            panel->Spacing = 2;

            panel->AddChild<Checkbox>("Fullscreen", Settings::Inferno.Fullscreen);

            _useVsync = Settings::Graphics.UseVsync;
            panel->AddChild<Checkbox>("VSync", _useVsync);
            panel->AddChild<Checkbox>("Procedural textures", Settings::Graphics.EnableProcedurals);
            panel->AddChild<Label>("");

            auto renderScale = make_unique<SliderFloat>("Render scale", 0.25f, 1.0f, Settings::Graphics.RenderScale, 2);
            //renderScale->LabelWidth = 300;
            renderScale->ShowValue = true;
            renderScale->LabelWidth = 250;
            renderScale->ValueWidth = 50;
            renderScale->OnChange = [](float value) {
                Settings::Graphics.RenderScale = std::floor(value * 20) / 20;
            };
            panel->AddChild(std::move(renderScale));

            auto fov = panel->AddChild<SliderFloat>("Field of view", 60.0f, 90.0f, Inferno::Settings::Graphics.FieldOfView, 0);
            fov->ShowValue = true;
            fov->LabelWidth = 250;
            fov->ValueWidth = 50;

            auto upscaleFilter = make_unique<OptionSpinner>("upscale filtering", std::initializer_list<string_view>{ "Sharp", "Smooth" }, (int&)Settings::Graphics.UpscaleFilter);
            upscaleFilter->LabelWidth = 340;
            panel->AddChild(std::move(upscaleFilter));

            auto filterMode = make_unique<OptionSpinner>("Texture Filtering", std::initializer_list<string_view>{ "None", "Enhanced", "Smooth" }, (int&)Settings::Graphics.FilterMode);
            filterMode->LabelWidth = 340;
            panel->AddChild(std::move(filterMode));

            _msaaSamples = [] {
                switch (Settings::Graphics.MsaaSamples) {
                    default:
                    case 1: return 0;
                    case 2: return 1;
                    case 4: return 2;
                    case 8: return 3;
                }
            }();

            auto msaa = make_unique<OptionSpinner>("MSAA", std::initializer_list<string_view>{ "None", "2x", "4x", "8x" }, _msaaSamples);
            msaa->LabelWidth = 340;
            panel->AddChild(std::move(msaa));

            panel->AddChild<Label>("");
            panel->AddChild<Label>("Framerate limits", FontSize::MediumBlue);
            panel->AddChild<Checkbox>("Enable Foreground Limit", Inferno::Settings::Graphics.EnableForegroundFpsLimit);

            auto foreground = panel->AddChild<Slider>("Foreground", 20, 240, Inferno::Settings::Graphics.ForegroundFpsLimit);
            foreground->ShowValue = true;
            foreground->LabelWidth = 200;
            foreground->ValueWidth = 40;

            auto background = panel->AddChild<Slider>("Background", 20, 60, Inferno::Settings::Graphics.BackgroundFpsLimit);
            background->ShowValue = true;
            background->LabelWidth = 200;
            background->ValueWidth = 40;

            AddChild(std::move(panel));
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

        bool HandleMenuAction(Input::MenuActionState action) override {
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

            panel->AddChild<Checkbox>("ship auto-leveling", Settings::Inferno.ShipAutolevel);

            panel->AddChild<Checkbox>("no weapon autoselect while firing", Settings::Inferno.NoAutoselectWhileFiring);
            panel->AddChild<Checkbox>("only cycle autoselect weapons", Settings::Inferno.OnlyCycleAutoselectWeapons);
            panel->AddChild<Checkbox>("sticky rearview", Settings::Inferno.StickyRearview);
            panel->AddChild<Checkbox>("charging fusion slows time", Settings::Inferno.SlowmoFusion);
            panel->AddChild<Checkbox>("prefer high res fonts", Settings::Inferno.PreferHighResFonts);

            panel->AddChild<Button>("Primary Weapon priority", [] {
                ShowScreen(make_unique<PriorityMenu>("Primary Priority", Settings::Inferno.PrimaryPriority, DEFAULT_PRIMARY_NAMES));
            });

            panel->AddChild<Button>("Secondary Weapon priority", [] {
                ShowScreen(make_unique<PriorityMenu>("Secondary Priority", Settings::Inferno.SecondaryPriority, DEFAULT_SECONDARY_NAMES));
            });

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
