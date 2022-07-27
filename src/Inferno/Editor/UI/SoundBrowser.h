#pragma once
#include "Editor/Editor.h"
#include "WindowBase.h"
#include "Resources.h"
#include "SoundSystem.h"

namespace Inferno::Editor {

    class SoundBrowser : public WindowBase {
        int _selection;
        float _vol = 0.1f, _pan = 0, _pitch = 0;
        bool _3d;
        Sound::Reverb _reverb{};

        const std::map<Sound::Reverb, const char*> ReverbLabels = {
            { Sound::Reverb::Off, "Off" },
            { Sound::Reverb::Default, "Default" },
            { Sound::Reverb::Generic, "Generic" },
            { Sound::Reverb::Room, "Room" },
            { Sound::Reverb::StoneRoom, "StoneRoom" },
            { Sound::Reverb::Cave, "Cave" },
            { Sound::Reverb::StoneCorridor, "StoneCorridor" },
            { Sound::Reverb::Quarry, "Quarry" },
            { Sound::Reverb::SewerPipe, "SewerPipe" },
            { Sound::Reverb::Underwater, "Underwater" },
            { Sound::Reverb::SmallRoom, "SmallRoom" },
            { Sound::Reverb::MediumRoom, "MediumRoom" },
            { Sound::Reverb::LargeRoom, "LargeRoom" },
            { Sound::Reverb::Hall, "Hall" },
            { Sound::Reverb::MediumHall, "MediumHall" },
            { Sound::Reverb::LargeHall, "LargeHall" },
            { Sound::Reverb::Plate, "Plate" },
        };

    public:
        SoundBrowser() : WindowBase("Sounds", &Settings::Windows.Sound) {}

    protected:
        void OnUpdate() override {
            auto& sounds = Resources::GameData.Sounds;

            ImGui::SliderFloat("Volume", &_vol, 0, 1);
            ImGui::SliderFloat("Pan", &_pan, -1, 1);
            ImGui::SliderFloat("Pitch", &_pitch, -1, 1);
            ImGui::Checkbox("3D", &_3d);

            if (ImGui::BeginCombo("Reverb", ReverbLabels.at(_reverb), ImGuiComboFlags_HeightLarge)) {
                for (auto& [item, text] : ReverbLabels) {
                    if (ImGui::Selectable(ReverbLabels.at(item), item == _reverb)) {
                        _reverb = item;
                        Sound::SetReverb(_reverb);
                    }
                }
                ImGui::EndCombo();
            }

            {
                ImGui::BeginChild("sounds", { -1, -1 }, true);

                for (int i = 0; i < sounds.size(); i++) {
                    auto idx = sounds[i];
                    if ((int)idx >= Resources::Sounds.Sounds.size()) continue;
                    auto& sound = Resources::Sounds.Sounds[(int)idx];

                    auto label = fmt::format("{}: {}", i, sound.Name);
                    if (ImGui::Selectable(label.c_str(), i == _selection)) {
                        _selection = i;
                        if (_3d)
                            Sound::Play3D((SoundID)i, Editor::Selection.Object, _vol, _pitch);
                        else
                            Sound::Play((SoundID)i, _vol, _pan, _pitch);
                    }
                }

                ImGui::EndChild();
            }
        }

    };

}