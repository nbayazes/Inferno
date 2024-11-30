#pragma once
#include "Game.UI.Controls.h"
#include "Graphics.h"
#include "Procedural.h"
#include "Resources.h"

namespace Inferno::UI {
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
            panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
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
                    deviceNames.push_back(Convert::ToString(device.description));
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
            panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;

            panel->AddChild<Checkbox>("Enable Mouse", Settings::Inferno.EnableMouse);
            panel->AddChild<Checkbox>("Enable joystick", Settings::Inferno.EnableJoystick);
            {
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
            panel->AddChild<Checkbox>("Classic pitch speed", Settings::Inferno.HalvePitchSpeed);

            panel->AddChild<Button>("Customize bindings");
            panel->AddChild<Button>("Keyboard sensitivity");
            panel->AddChild<Button>("Mouse sensitivity");
            panel->AddChild<Button>("Joystick sensitivity");

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
            panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
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

    class GameOptionsMenu : public DialogBase {
    public:
        GameOptionsMenu() : DialogBase("Game Options") {
            Size = Vector2(620, 460);
            CloseOnConfirm = false;

            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
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

            panel->AddChild<Button>("Primary autoselect ordering");
            panel->AddChild<Button>("Secondary autoselect ordering");

            AddChild(std::move(panel));
        }
    };

    ScreenBase* ShowScreen(Ptr<ScreenBase> screen);

    class OptionsMenu : public DialogBase {
    public:
        OptionsMenu() : DialogBase("Options") {
            Size = Vector2(200, 30 * 4 + DIALOG_CONTENT_PADDING + DIALOG_PADDING);
            CloseOnConfirm = false;
            //TitleAlignment = AlignH::Left;

            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
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
