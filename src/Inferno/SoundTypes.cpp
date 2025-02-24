#include "pch.h"
#include "SoundTypes.h"
#include "Game.h"
#include "Resources.h"

namespace Inferno {
    SoundResource::SoundResource(SoundID id) {
        auto& table = Resources::ResolveGameData(Game::Level.IsDescent1() ? FullGameData::Descent1 : FullGameData::Descent2);

        if (auto item = Seq::tryItem(table.Sounds, (int)id)) {
            if (Game::Level.IsDescent1())
                D1 = *item;
            else
                D2 = *item;
        }
    }

    SoundResource::SoundResource(string name) : D3(std::move(name)) {}

    float SoundResource::GetDuration() const {
        if (!D3.empty()) {
            ASSERT(false); // no way to get the duration without reading the wav?
            if (auto info = Resources::ReadOutrageSoundInfo(D3)) {}
        }
        else if (D1 != -1) {
            if (auto sound = Seq::tryItem(Resources::GameData.sounds.Sounds, D1))
                return (float)sound->Length / Resources::GameData.sounds.Frequency;
        }
        else if (D2 != -1) {
            if (auto sound = Seq::tryItem(Resources::GameData.sounds.Sounds, D2))
                return (float)sound->Length / Resources::GameData.sounds.Frequency;
        }

        return 0;
    }
}
