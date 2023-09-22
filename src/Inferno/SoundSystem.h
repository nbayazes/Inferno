#pragma once
#include <chrono>
#include "Utility.h"
#include "SoundTypes.h"

namespace Inferno {
    struct AmbientSoundEmitter {
        List<string> Sounds; // List of sounds to play at random
        NumericRange<float> Delay; // Time between each sound
        NumericRange<float> Volume{ 1, 1 };

        float Life = 60 * 60 * 60;
        double NextPlayTime = 0;
        float Distance = 1000; // When > 0, enables random 3D positioning of sources

        static bool IsAlive(const AmbientSoundEmitter& e) { return e.Life > 0; }
    };
}

namespace Inferno::Sound {
    void Init(HWND, std::chrono::milliseconds pollRate = std::chrono::milliseconds(5));

    // Blocks until the sound system is initialized
    void WaitInitialized();
    void Shutdown();

    void Play(const SoundResource& resource, float volume = 1, float pan = 0, float pitch = 0);
    SoundUID Play(const Sound3D& sound);

    // Resets any cached sounds after loading a level
    void Reset();

    enum class Reverb {
        Off,
        Default = 1,
        Generic = 2,
        PaddedCell = 4,
        Room = 5,
        Bathroom = 6,
        StoneRoom = 8,
        Cave = 11,
        Arena = 12,
        Hangar = 13,
        Hall = 15,
        StoneCorridor = 16,
        Alley = 17,
        City = 18,
        Mountains = 19,
        Quarry = 20,
        SewerPipe = 23,
        Underwater = 24,
        SmallRoom = 25,
        MediumRoom = 26,
        LargeRoom = 27,
        MediumHall = 28,
        LargeHall = 29,
        Plate = 30
    };

    void SetReverb(Reverb);

    void Pause();
    void Resume();
    float GetVolume();
    void SetVolume(float volume);
    void Stop3DSounds();
    void Stop2DSounds();
    void Stop(Tag);
    void Stop(SoundUID);
    void Stop(ObjRef);

    void AddEmitter(AmbientSoundEmitter&&);
    void UpdateSoundEmitters(float dt);

    namespace Debug {
        inline List<Vector3> Emitters;
    }
}
