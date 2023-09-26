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
}
