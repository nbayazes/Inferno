#pragma once
#include <chrono>
#include <windef.h>
#include "Types.h"

namespace Inferno::Sound {
    void Init(HWND, std::chrono::milliseconds pollRate = std::chrono::milliseconds(10));
    void Shutdown();
    void Play(SoundID id, float volume, float pitch, float pan);
    void ClearCache();
}