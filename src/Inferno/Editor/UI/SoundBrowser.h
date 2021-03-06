#pragma once
#include "Editor/Editor.h"
#include "WindowBase.h"
#include "Resources.h"
#include "SoundSystem.h"

namespace Inferno::Editor {

    class SoundBrowser : public WindowBase {
        int _selection;
        float _vol = 0.1f, _pan = 0, _pitch = 0;
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
            auto& sounds = Resources::Sounds.Sounds;

            ImGui::SliderFloat("Volume", &_vol, 0, 1);
            ImGui::SliderFloat("Pan", &_pan, -1, 1);
            ImGui::SliderFloat("Pitch", &_pitch, -1, 1);

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
                    auto label = fmt::format("{}: {}", i, sounds[i].Name);
                    if (ImGui::Selectable(label.c_str(), i == _selection)) {
                        _selection = i;
                        Sound::Play3D((SoundID)_selection, _vol, Editor::Selection.Object, _pitch);
                    }
                }

                ImGui::EndChild();
            }
        }

    };

}