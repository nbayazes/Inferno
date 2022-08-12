#pragma once
#include <chrono>
#include <windef.h>
#include "Types.h"
#include "Camera.h"

namespace Inferno::Sound {
    // Sound source priority: D3, D1, D2
    // D1 has higher priority than D2
    struct SoundResource {
        int D1 = -1; // Index to PIG data
        int D2 = -1; // Index to S22 data
        string D3; // D3 file name or system path

        size_t GetID() const {
            if (!D3.empty()) return std::hash<string>{}(D3);
            else if(D1 != -1) return D1;
            else if(D2 != -1) return 1000 + D2;
            return 0;
        }
    };

    struct Sound2D {
        float Volume = 1;
        float Pitch = 0;
        SoundResource Resource;
    };

    struct Sound3D {
        Sound3D(ObjID source) : Source(source) { }
        Sound3D(const Vector3& pos, SegID seg) : Position(pos), Segment(seg) {}

        Vector3 Position; // Position the sound comes from
        SegID Segment = SegID::None; // Segment the sound starts in, needed for occlusion
        ObjID Source = ObjID::None; // Source to attach the sound to
        float Volume = 1;
        float Pitch = 0;
        SoundResource Resource;
        bool AttachToSource = false;
        Vector3 AttachOffset;
        bool FromPlayer = false; // For the player's firing sounds, afterburner, etc
    };
    
    void Init(HWND, float volume = 1, std::chrono::milliseconds pollRate = std::chrono::milliseconds(5));
    void Shutdown();
    void Play(const SoundResource& resource, float volume = 1, float pan = 0, float pitch = 0);
    void Play(const Sound3D& sound);
    void UpdateEmitterPositions(float dt);

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

    constexpr auto SOUND_WEAPON_HIT_DOOR = SoundID(27);

    namespace Debug {
        inline List<Vector3> Emitters;
    }
}
