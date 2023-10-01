#include "pch.h"
#include "SoundTypes.h"
#include "Game.h"
#include "Resources.h"

namespace Inferno {
    SoundResource::SoundResource(SoundID id) {
        if (!Seq::inRange(Resources::GameData.Sounds, (int)id))
            return;

        if (Game::Level.IsDescent1())
            D1 = Resources::GameData.Sounds[(int)id];
        else
            D2 = Resources::GameData.Sounds[(int)id];
    }

    SoundResource::SoundResource(string name) : D3(std::move(name)) { }

    float SoundResource::GetDuration() const {
        if (!D3.empty()) {
            ASSERT(false); // no way to get the duration without reading the wav?
            if (auto info = Resources::ReadOutrageSoundInfo(D3)) { }
        }
        else if (D1 != -1) {
            return (float)Resources::SoundsD1.Sounds[D1].Length / Resources::SoundsD1.Frequency;
        }
        else if (D2 != -1) {
            return (float)Resources::SoundsD2.Sounds[D2].Length / Resources::SoundsD2.Frequency;
        }

        return 0;
    }
}
