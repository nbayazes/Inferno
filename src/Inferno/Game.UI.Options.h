#pragma once
#include "Game.UI.Controls.h"
#include "Graphics.h"
#include "Procedural.h"
#include "Resources.h"

namespace Inferno::UI {
    class SoundOptionsMenu : public DialogBase {
        List<AudioEngine::RendererDetail> _devices;
        int _deviceIndex = 0;

    public:
        SoundOptionsMenu() : DialogBase("Sound Options") {
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
                device->ShowValue = false;
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


            //try {
            //    _devices = AudioEngine::GetRendererDetails();
            //    for (auto& device : _devices) {
            //        //panel->AddChild<Label>(Convert::ToString(device.description));
            //        auto button = make_unique<Button>(Convert::ToString(device.description), [&device] {
            //            Sound::Init(Shell::Hwnd, &device.deviceId);
            //            Game::PlayMainMenuMusic();
            //        });
            //        button->ActionSound = ""; // Clear action sound, as switching sound devices orphans the one-shot effect which results in a warning
            //        panel->AddChild(std::move(button));
            //    }
            //}
            //catch (...) {
            //    SPDLOG_ERROR("Error getting sound devices");
            //}

            AddChild(std::move(panel));
        }
    };

    class OptionsMenu : public DialogBase {
        int _msaaSamples = 0;

    public:
        OptionsMenu() : DialogBase("Options") {
            Size = Vector2(620, 460);
            CloseOnConfirm = false;

            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;
            panel->Spacing = 2;
            //panel->AddChild<Label>("Options", FontSize::MediumBlue);

            auto bombSound = Seq::tryItem(Inferno::Resources::GameDataD1.Sounds, (int)SoundID::DropBomb);

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

            panel->AddChild<Checkbox>("Classic pitch speed", Settings::Inferno.HalvePitchSpeed);

            {
                auto fullscreen = make_unique<Checkbox>("Fullscreen", Settings::Inferno.Fullscreen);
                panel->AddChild(std::move(fullscreen));
            }


            panel->AddChild<Checkbox>("Procedural textures", Settings::Graphics.EnableProcedurals);

            // filtering
            //ImGui::Combo("Filtering", (int*)&Settings::Graphics.FilterMode, "Point\0Enhanced point\0Smooth");
            auto renderScale = make_unique<SliderFloat>("Render scale", 0.25f, 1.0f, Settings::Graphics.RenderScale, 2);
            //renderScale->LabelWidth = 300;
            renderScale->ShowValue = true;
            renderScale->ValueWidth = 50;
            renderScale->OnChange = [](float value) {
                Settings::Graphics.RenderScale = std::floor(value * 20) / 20;
            };

            panel->AddChild(std::move(renderScale));

            //auto filterMode = make_unique<SliderSelect>("Filter:", std::initializer_list<string_view>{ "Point", "Enhanced", "Smooth" }, (int&)Settings::Graphics.FilterMode);
            //panel->AddChild(std::move(filterMode));

            auto filterMode = make_unique<OptionSpinner>("Texture Filtering", std::initializer_list<string_view>{ "Point", "Enhanced", "Smooth" }, (int&)Settings::Graphics.FilterMode);
            filterMode->LabelWidth = 320;
            panel->AddChild(std::move(filterMode));

            auto wiggle = make_unique<OptionSpinner>("Ship Wiggle", std::initializer_list<string_view>{ "None", "Reduced", "Normal" }, (int&)Settings::Inferno.ShipWiggle);
            wiggle->LabelWidth = 320;
            panel->AddChild(std::move(wiggle));

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
            msaa->LabelWidth = 320;
            panel->AddChild(std::move(msaa));

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
        }
    };
}
