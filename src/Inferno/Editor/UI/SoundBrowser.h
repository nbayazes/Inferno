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
        List<char> _search;
        List<SoundID> _d1Lookup, _d2Lookup;
        int _selectedGame = 0;

    public:
        SoundBrowser() : WindowBase("Sounds", &Settings::Editor.Windows.Sound) {
            _search.resize(50);
        }

    protected:
        void OnOpen(bool) override {
            if (Game::Level.IsDescent2()) {
                _selectedGame = 1;
                Resources::LoadDescent2Data();
            }
        }

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

            if (ImGui::BeginCombo("Reverb", Sound::REVERB_LABELS.at(_reverb), ImGuiComboFlags_HeightLarge)) {
                for (const auto& item : Sound::REVERB_LABELS | views::keys) {
                    if (ImGui::Selectable(Sound::REVERB_LABELS.at(item), item == _reverb)) {
                        _reverb = item;
                        Sound::SetReverb(_reverb);
                    }
                }
                ImGui::EndCombo();
            }


            if (ImGui::Combo("Game", &_selectedGame, "Descent 1\0Descent 2\0Descent 3\0")) {
                if (_selectedGame == 0) {
                    if (!Resources::LoadDescent1Data())
                        Resources::LoadDescent1DemoData();
                }
                else if (_selectedGame == 1) {
                    Resources::LoadDescent2Data();
                }
            }

            ImGui::Separator();
            ImGui::Text("Search");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##Search", _search.data(), _search.capacity());

            auto searchstr = String::ToLower(string(_search.data()));

            {
                ImGui::BeginChild("sounds", { -1, -1 }, true);

                auto listSounds = [&](const FullGameData& game, const span<SoundID> lookup) {
                    for (int i = 0; i < game.sounds.Sounds.size(); i++) {
                        auto& sound = game.sounds.Sounds[i];

                        if (!searchstr.empty()) {
                            if (!String::Contains(String::ToLower(sound.Name), searchstr))
                                continue;
                        }

                        string label;
                        if (lookup.size() <= i)
                            label = fmt::format("{}: {}", i, sound.Name);
                        else
                            label = fmt::format("{} [{}]: {}", i, (int)lookup[i], sound.Name);

                        if (ImGui::Selectable(label.c_str(), i == _selection)) {
                            _selection = i;
                            SoundResource resource;

                            if (_selectedGame == 0)
                                resource.D1 = i;
                            else
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
                };

                if (_selectedGame == 0) {
                    auto& data = Resources::ResolveGameData(FullGameData::Descent1);
                    if (_d1Lookup.empty())
                        _d1Lookup = CreateSoundIdLookup(data);

                    listSounds(data, _d1Lookup);
                }
                else if (_selectedGame == 1) {
                    auto& data = Resources::ResolveGameData(FullGameData::Descent2);

                    if (_d2Lookup.empty())
                        _d2Lookup = CreateSoundIdLookup(data);

                    listSounds(data, _d2Lookup);
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
        // Creates a reverse lookup of sound ids
        static List<SoundID> CreateSoundIdLookup(const FullGameData& data) {
            List<SoundID> lookup;
            auto& headers = data.sounds.Sounds;
            lookup.resize(headers.size());
            ranges::fill(lookup, SoundID::None);

            for (int i = 0; i < headers.size(); i++) {
                for (int soundId = 0; soundId < data.Sounds.size(); soundId++) {
                    if (i == data.Sounds[soundId])
                        lookup[i] = (SoundID)soundId;
                }
            }

            return lookup;
        }
    };
}
