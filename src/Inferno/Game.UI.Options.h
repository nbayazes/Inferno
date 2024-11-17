#pragma once
#include "Game.UI.Controls.h"

namespace Inferno::UI {
    //inline std::array VOLUME_TABLE = { 1.0f, .707f, .5f, .35f, .25f, .18f, .09f, .045f, 0.0225f, 0.0f };

    class OptionsMenu : public DialogBase {
        int _value = 9;
        int _value2 = 5;
        int _value3 = 5;

    public:
        OptionsMenu() : DialogBase("Options") {
            Size = Vector2(500, 460);
            CloseOnConfirm = false;

            auto panel = make_unique<StackPanel>();
            panel->Size.x = Size.x - DIALOG_PADDING * 2;
            panel->Position = Vector2(0, DIALOG_CONTENT_PADDING);
            panel->HorizontalAlignment = AlignH::Center;
            panel->VerticalAlignment = AlignV::Top;
            panel->Spacing = 2;
            //panel->AddChild<Label>("Options", FontSize::MediumBlue);


            _value = (int)std::floor(Settings::Inferno.MasterVolume * 10);
            auto volume = make_unique<Slider>("Master Volume", 0, 10, _value);
            volume->BarOffset = 250;
            volume->ChangeSound = MENU_SELECT_SOUND;
            volume->OnChange = [](int value) {
                Settings::Inferno.MasterVolume = value / 10.0f;
                Sound::SetMasterVolume(Settings::Inferno.MasterVolume);
                //Sound::SetMasterVolume(VOLUME_TABLE[value]);

                //Sound::SetMasterVolume(value / 10.0f);
            };
            panel->AddChild(std::move(volume));

            _value2 = (int)std::floor(Settings::Inferno.EffectVolume * 10);
            auto fxVolume = make_unique<Slider>("FX Volume", 0, 10, _value2);
            fxVolume->BarOffset = 250;
            fxVolume->OnChange = [](int value) {
                Settings::Inferno.EffectVolume = value / 10.0f;
                //Settings::Inferno.EffectVolume = XAudio2DecibelsToAmplitudeRatio(-50.0f * (1 - value / 10.0f));
                Sound::SetEffectVolume(Settings::Inferno.EffectVolume);
                //Settings::Inferno.EffectVolume = VOLUME_TABLE[value];
                //Sound::SetEffectVolume(VOLUME_TABLE[value]);
                //Sound::SetEffectVolume(value / 10.0f);
            };

            fxVolume->ChangeSound = MENU_SELECT_SOUND;
            panel->AddChild(std::move(fxVolume));


            _value3 = (int)std::floor(Settings::Inferno.MusicVolume * 10);
            auto music = make_unique<Slider>("Music Volume", 0, 10, _value3);
            music->BarOffset = 250;
            music->OnChange = [](int value) {
                Settings::Inferno.MusicVolume = value / 10.0f;
                Sound::SetMusicVolume(Settings::Inferno.MusicVolume);
                //Sound::SetMusicVolume(MUSIC_VOLUME_TABLE[value] * .5f);
                //auto volume = XAudio2DecibelsToAmplitudeRatio(-60.0f * (1 - value / 10.0f));
                //Sound::SetMusicVolume(volume);
                //Sound::SetMusicVolume(value);
            };
            panel->AddChild(std::move(music));
            AddChild(std::move(panel));
        }
    };
}
