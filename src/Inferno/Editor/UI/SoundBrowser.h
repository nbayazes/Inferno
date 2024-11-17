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

        std::map<Sound::Reverb, const char*> ReverbLabels = {
            { Sound::Reverb::Off, "Off" },
            { Sound::Reverb::Default, "Default" },
            { Sound::Reverb::Generic, "Generic" },
            { Sound::Reverb::PaddedCell, "PaddedCell" },
            { Sound::Reverb::Room, "Room" },
            { Sound::Reverb::Bathroom, "Bathroom" },
            { Sound::Reverb::StoneRoom, "StoneRoom" },
            { Sound::Reverb::Cave, "Cave" },
            { Sound::Reverb::Arena, "Arena" },
            { Sound::Reverb::Hangar, "Hangar" },
            { Sound::Reverb::Hall, "Hall" },
            { Sound::Reverb::StoneCorridor, "StoneCorridor" },
            { Sound::Reverb::Alley, "Alley" },
            { Sound::Reverb::City, "City" },
            { Sound::Reverb::Mountains, "Mountains" },
            { Sound::Reverb::Quarry, "Quarry" },
            { Sound::Reverb::SewerPipe, "SewerPipe" },
            { Sound::Reverb::Underwater, "Underwater" },
            { Sound::Reverb::SmallRoom, "SmallRoom" },
            { Sound::Reverb::MediumRoom, "MediumRoom" },
            { Sound::Reverb::LargeRoom, "LargeRoom" },
            { Sound::Reverb::MediumHall, "MediumHall" },
            { Sound::Reverb::LargeHall, "LargeHall" },
            { Sound::Reverb::Plate, "Plate" },
        };

        List<SoundID> _soundIdLookup;
        int _selectedGame = 0;

    public:
        SoundBrowser() : WindowBase("Sounds", &Settings::Editor.Windows.Sound) {}

    protected:
        void OnUpdate() override {
            if (ImGui::SliderFloat("Master Volume", &Settings::Inferno.MasterVolume, 0, 1))
                Sound::SetMasterVolume(Settings::Inferno.MasterVolume);

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


            if (ImGui::Combo("Game", &_selectedGame, "Descent 1\0Descent 2\0Descent 3\0"))
                _soundIdLookup.clear();

            if (_selectedGame != 2 && _soundIdLookup.empty())
                _soundIdLookup = UpdateSoundIdLookup();

            static List<char> search(50);
            ImGui::Separator();
            ImGui::Text("Search");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Search", search.data(), search.capacity());

            auto searchstr = String::ToLower(string(search.data()));

            {
                ImGui::BeginChild("sounds", { -1, -1 }, true);

                if (_selectedGame == 0) {
                    for (int i = 0; i < Resources::SoundsD1.Sounds.size(); i++) {
                        auto& sound = Resources::SoundsD1.Sounds[i];

                        if (!searchstr.empty()) {
                            if (!String::Contains(String::ToLower(sound.Name), searchstr))
                                continue;
                        }

                        // todo: update soundIdLookup when level changes
                        string label;
                        if (_soundIdLookup.size() <= i)
                            label = fmt::format("{}: {}", i, sound.Name);
                        else
                            label = fmt::format("{} [{}]: {}", i, (int)_soundIdLookup[i], sound.Name);

                        if (ImGui::Selectable(label.c_str(), i == _selection)) {
                            _selection = i;
                            SoundResource resource;
                            resource.D1 = i;

                            if (_3d) {
                                if (auto obj = Game::Level.TryGetObject(Editor::Selection.Object)) {
                                    Sound3D s(resource);
                                    s.Volume = _vol;
                                    s.Pitch = _pitch;
                                    Sound::PlayFrom(s, *obj);
                                }
                            }
                            else {
                                Sound::Play2D(resource, _vol, _pan, _pitch);
                            }
                        }
                    }
                }
                else if (_selectedGame == 1) {
                    for (int i = 0; i < Resources::SoundsD2.Sounds.size(); i++) {
                        auto& sound = Resources::SoundsD2.Sounds[i];

                        if (!searchstr.empty()) {
                            if (!String::Contains(String::ToLower(sound.Name), searchstr))
                                continue;
                        }

                        // todo: update soundIdLookup when level changes
                        string label;
                        if (_soundIdLookup.size() <= i)
                            label = fmt::format("{}: {}", i, sound.Name);
                        else
                            label = fmt::format("{} [{}]: {}", i, (int)_soundIdLookup[i], sound.Name);

                        if (ImGui::Selectable(label.c_str(), i == _selection)) {
                            _selection = i;
                            SoundResource resource;
                            resource.D2 = i;

                            if (_3d) {
                                if (auto obj = Game::Level.TryGetObject(Editor::Selection.Object)) {
                                    Sound3D s(resource);
                                    s.Volume = _vol;
                                    s.Pitch = _pitch;
                                    Sound::PlayFrom(s, *obj);
                                }
                            }
                            else {
                                Sound::Play2D(resource, _vol, _pan, _pitch);
                            }
                        }
                    }
                }
                else if (_selectedGame == 2) {
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
                                if (auto obj = Game::Level.TryGetObject(Editor::Selection.Object)) {
                                    Sound3D s(sound.FileName);
                                    s.Volume = _vol;
                                    s.Pitch = _pitch;
                                    Sound::PlayFrom(s, *obj);
                                }
                            }
                            else {
                                Sound::Play2D({ sound.FileName }, _vol, _pan, _pitch);
                            }
                        }
                    }
                }

                ImGui::EndChild();
            }
        }

    private:
        List<SoundID> UpdateSoundIdLookup() const {
            List<SoundFile::Header>* headers = nullptr;
            List<SoundID> lookup;

            if (_selectedGame == 0 && Game::Level.IsDescent1())
                headers = &Resources::SoundsD1.Sounds;
            else if (_selectedGame == 1 && Game::Level.IsDescent2())
                headers = &Resources::SoundsD2.Sounds;

            if (!headers) return lookup;
            lookup.resize(headers->size());
            ranges::fill(lookup, SoundID::None);

            for (int i = 0; i < headers->size(); i++) {
                for (int soundId = 0; soundId < Resources::GameData.Sounds.size(); soundId++) {
                    if (i == Resources::GameData.Sounds[soundId])
                        lookup[i] = (SoundID)soundId;
                }
            }

            return lookup;
        }
    };
}
