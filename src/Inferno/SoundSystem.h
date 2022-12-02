#pragma once
#include <chrono>
#include <windef.h>
#include "Types.h"
#include "Camera.h"

namespace Inferno {
    using SoundUID = unsigned int; // ID used to cancel a playing sound

    // Handle to a sound resource
    struct SoundResource {
        int D1 = -1; // Index to PIG data
        int D2 = -1; // Index to S22 data
        string D3; // D3 file name or system path

        // Priority is D3, D1, D2
        bool operator== (const SoundResource& rhs) const {
            if (!D3.empty() && !rhs.D3.empty()) return D3 == rhs.D3;
            if (D1 != -1 && rhs.D1 != -1) return D1 == rhs.D1;
            return D2 == rhs.D2;
        }
    };

    struct Sound2D {
        float Volume = 1;
        float Pitch = 0;
        SoundResource Resource;
    };

    struct Sound3D {
        Sound3D(ObjID source) : Source(source) {}
        Sound3D(const Vector3& pos, SegID seg) : Position(pos), Segment(seg) {}

        Vector3 Position; // Position the sound comes from
        SegID Segment = SegID::None; // Segment the sound starts in, needed for occlusion
        SideID Side = SideID::None; // Side, used for turning of forcefields
        ObjID Source = ObjID::None; // Source to attach the sound to
        float Volume = 1;
        float Pitch = 0; // -1 to 1;
        bool Occlusion = true; // Occludes level geometry when determining volume
        float Radius = 250; // Determines max range and falloff
        SoundResource Resource;
        bool AttachToSource = false; // The sound moves with the Source object
        Vector3 AttachOffset; // The offset from the Source when attached
        bool FromPlayer = false; // For the player's firing sounds, afterburner, etc
        SoundUID ID = 0;
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
    void Shutdown();

    void Play(const SoundResource& resource, float volume = 1, float pan = 0, float pitch = 0);
    SoundUID Play(const Sound3D& sound);

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
    float GetVolume();
    void SetVolume(float volume);
    void Stop3DSounds();
    void Stop2DSounds();
    void Stop(Tag);
    void Stop(SoundUID);
    void Stop(ObjID);

    void AddEmitter(AmbientSoundEmitter&&);
    void UpdateSoundEmitters(float dt);

    namespace Debug {
        inline List<Vector3> Emitters;
    }
}
