#pragma once
#include <chrono>
#include <windef.h>
#include "Types.h"
#include "Camera.h"

namespace Inferno::Sound {
    void Init(HWND, std::chrono::milliseconds pollRate = std::chrono::milliseconds(10));
    void Shutdown();
    void Play(SoundID id, float volume = 1, float pan = 0, float pitch = 0);
    void Play3D(SoundID id, float volume, ObjID source, float pitch);

    // Resets any cached sounds after loading a level
    void Reset();

    enum class Reverb {
        Off,
        Default = 1,
        Generic = 2,
        Room = 5,
        StoneRoom = 8,
        Cave = 11,
        StoneCorridor = 16,
        Quarry = 20,
        SewerPipe = 23,
        Underwater = 24,
        SmallRoom = 25,
        MediumRoom = 26,
        LargeRoom = 27,
        Hall = 15,
        MediumHall = 28,
        LargeHall = 29,
        Plate = 30,
        Count
    };

    void SetReverb(Reverb);

    void Pause();
    void Resume();
}
