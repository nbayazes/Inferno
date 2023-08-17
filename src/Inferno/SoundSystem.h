#pragma once
#include <chrono>
#include "Utility.h"
#include "SoundTypes.h"

namespace Inferno {
    struct Sound3D {
        Sound3D(ObjID source) : Source(source) {}
        Sound3D(const Vector3& pos, SegID seg) : Position(pos), Segment(seg) {}

        Vector3 Position; // Position the sound comes from
        SegID Segment = SegID::None; // Segment the sound starts in, needed for occlusion
        SideID Side = SideID::None; // Side, used for turning of forcefields
        ObjRef Source = GLOBAL_SOUND_SOURCE; // Source to attach the sound to
        float Volume = 1;
        float Pitch = 0; // -1 to 1;
        bool Occlusion = true; // Occludes level geometry when determining volume
        float Radius = DEFAULT_SOUND_RADIUS; // Determines max range and falloff
        SoundResource Resource;
        bool AttachToSource = false; // The sound moves with the Source object
        Vector3 AttachOffset; // The offset from the Source when attached
        bool FromPlayer = false; // For the player's firing sounds, afterburner, etc
        bool Merge = true; // Merge with other sounds played in a similar timeframe
        SoundUID ID = SoundUID::None;
        bool Looped = false;
        uint32 LoopCount = 0;
        uint32 LoopStart = 0;
        uint32 LoopEnd = 0;
    };

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
