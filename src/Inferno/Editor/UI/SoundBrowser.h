#pragma once
#include "Editor/Editor.h"
#include "WindowBase.h"
#include "Resources.h"
#include "SoundSystem.h"

namespace Inferno::Editor {

    class SoundBrowser final : public WindowBase {
        int _selection = 0;
        float _vol = 1, _pan = 0, _pitch = 0;
        bool _3d = false;
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
        SoundBrowser() : WindowBase("Sounds", &Settings::Editor.Windows.Sound) {}

    protected:
        void OnUpdate() override {
            auto masterVol = Sound::GetVolume();
            if (ImGui::SliderFloat("Master Volume", &masterVol, 0, 1))
                Sound::SetVolume(masterVol);

            ImGui::Checkbox("3D", &_3d);

            {
                DisableControls disable(_3d);
                ImGui::SliderFloat("Volume", &_vol, 0, 1);
                ImGui::SliderFloat("Pan", &_pan, -1, 1);
            }

            ImGui::SliderFloat("Pitch", &_pitch, -1, 1);

            {
                DisableControls disable(!_3d);
                if (ImGui::Button("Stop sounds")) Sound::Stop3DSounds();
            }

            if (ImGui::BeginCombo("Reverb", ReverbLabels.at(_reverb), ImGuiComboFlags_HeightLarge)) {
                for (const auto& item : ReverbLabels | views::keys) {
                    if (ImGui::Selectable(ReverbLabels.at(item), item == _reverb)) {
                        _reverb = item;
                        Sound::SetReverb(_reverb);
                    }
                }
                ImGui::EndCombo();
            }


            static int selectedGame = 0;
            ImGui::Combo("Game", &selectedGame, "Descent 1\0Descent 2\0Descent 3\0");

            static List<char> search(50);
            ImGui::Separator();
            ImGui::Text("Search");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("Search", search.data(), 50);

            Sound::Sound3D s(Editor::Selection.Object);
            s.Volume = _vol;
            s.Pitch = _pitch;
            s.AttachToSource = true;
            auto searchstr = String::ToLower(string(search.data()));

            {
                ImGui::BeginChild("sounds", { -1, -1 }, true);

                if (selectedGame == 0) {
                    for (int i = 0; i < Resources::SoundsD1.Sounds.size(); i++) {
                        auto& sound = Resources::SoundsD1.Sounds[i];

                        if (!searchstr.empty()) {
                            if (!String::Contains(String::ToLower(sound.Name), searchstr))
                                continue;
                        }

                        auto label = fmt::format("{}: {}", i, sound.Name);
                        if (ImGui::Selectable(label.c_str(), i == _selection)) {
                            _selection = i;
                            if (_3d) {
                                s.Resource = { .D1 = i };
                                Sound::Play(s);
                            }
                            else {
                                Sound::Play({ .D1 = i }, _vol, _pan, _pitch);
                            }
                        }
                    }
                }
                else if (selectedGame == 1) {
                    for (int i = 0; i < Resources::SoundsD2.Sounds.size(); i++) {
                        auto& sound = Resources::SoundsD2.Sounds[i];

                        if (!searchstr.empty()) {
                            if (!String::Contains(String::ToLower(sound.Name), searchstr))
                                continue;
                        }

                        auto label = fmt::format("{}: {}", i, sound.Name);
                        if (ImGui::Selectable(label.c_str(), i == _selection)) {
                            _selection = i;
                            if (_3d) {
                                s.Resource = { .D2 = i };
                                Sound::Play(s);
                            }
                            else {
                                Sound::Play({ .D2 = i }, _vol, _pan, _pitch);
                            }
                        }
                    }
                }
                else if (selectedGame == 2) {
                    for (int i = 0; i < Resources::GameTable.Sounds.size(); i++) {
                        auto& sound = Resources::GameTable.Sounds[i];

                        auto label = fmt::format("{}: {} ({})", i, sound.Name, sound.FileName);

                        if (!searchstr.empty()) {
                            if (!String::Contains(String::ToLower(label), searchstr))
                                continue;
                        }

                        if (ImGui::Selectable(label.c_str(), i == _selection)) {
                            _selection = i;
                            if (_3d) {
                                s.Resource = { .D3 = sound.FileName };
                                Sound::Play(s);
                            }
                            else {
                                Sound::Play({ .D3 = sound.FileName }, _vol, _pan, _pitch);
                            }
                        }
                    }
                }

                ImGui::EndChild();
            }
        }
    };
}