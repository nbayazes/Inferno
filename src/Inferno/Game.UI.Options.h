#pragma once
#include "Game.UI.Controls.h"
#include "Procedural.h"
#include "Resources.h"

namespace Inferno::UI {
    //inline std::array VOLUME_TABLE = { 1.0f, .707f, .5f, .35f, .25f, .18f, .09f, .045f, 0.0225f, 0.0f };

    class OptionsMenu : public DialogBase {
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


            auto procedurals = make_unique<Checkbox>("Procedural textures", Settings::Graphics.EnableProcedurals);
            procedurals->ClickAction = [] { EnableProceduralTextures(Settings::Graphics.EnableProcedurals); };
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

            AddChild(std::move(panel));
        }
    };
}
