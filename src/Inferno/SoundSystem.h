#pragma once
#include <chrono>
#include "Audio/Audio.h"
#include "Object.h"
#include "SoundTypes.h"
#include "Utility.h"

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
    constexpr float SOUND_MERGE_RATIO = 0.025f; // percentage added to existing sounds when merged

    void Init(HWND, const wstring* deviceId = nullptr, std::chrono::milliseconds pollRate = std::chrono::milliseconds(4));

    void CopySoundIds();

    // Blocks until the sound system is initialized
    void WaitInitialized();
    void Shutdown();

    void Play2D(const SoundResource& resource, float volume = 1, float pan = 0, float pitch = 0);

    //SoundUID Play(const Sound3D& sound);

    SoundUID Play(const Sound3D& sound, const Vector3& position, SegID seg, SideID side = SideID::None);

    // Plays a sound from an object position
    SoundUID Play(const Sound3D& sound, const Object& source);

    // Plays a sound attached to an object that stops if it is destroyed
    SoundUID PlayFrom(const Sound3D& sound, const Object& source);

    // Plays a raw music file. Returns true if playback started. Ogg, flac, and MP3 are supported.
    bool PlayMusic(const List<byte>&& data, bool loop);


    // Stops the currently playing music
    void StopMusic();

    void SetMusicVolume(float volume);

    void PauseSounds();

    void ResumeSounds();

    // Resets any cached sounds after loading a level
    void StopAllSounds();

    void UnloadD1Sounds();
    void UnloadNamedSounds();

    enum class Reverb : uint8 {
        Off = 0,
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

    inline std::map<Sound::Reverb, const char*> REVERB_LABELS = {
        { Reverb::Off, "Off" },
        { Reverb::Default, "Default" },
        { Reverb::Generic, "Generic" },
        { Reverb::PaddedCell, "Padded cell" },
        { Reverb::Room, "Room" },
        { Reverb::Bathroom, "Bathroom" },
        { Reverb::StoneRoom, "Stone room" },
        { Reverb::Cave, "Cave" },
        { Reverb::Arena, "Arena" },
        { Reverb::Hangar, "Hangar" },
        { Reverb::Hall, "Hall" },
        { Reverb::StoneCorridor, "Stone corridor" },
        { Reverb::Alley, "Alley" },
        { Reverb::City, "City" },
        { Reverb::Mountains, "Mountains" },
        { Reverb::Quarry, "Quarry" },
        { Reverb::SewerPipe, "Sewer pipe" },
        { Reverb::Underwater, "Underwater" },
        { Reverb::SmallRoom, "Small room" },
        { Reverb::MediumRoom, "Medium room" },
        { Reverb::LargeRoom, "Large room" },
        { Reverb::MediumHall, "Medium hall" },
        { Reverb::LargeHall, "Large hall" },
        { Reverb::Plate, "Plate" },
    };

    AudioEngine* GetEngine();
    void SetReverb(Reverb);

    void Pause();
    void Resume();
    //float GetVolume();

    // Volume should be in the range 0.0 to 1.0
    void SetMasterVolume(float volume);

    // Volume should be in the range 0.0 to 1.0
    void SetEffectVolume(float volume);

    void Stop3DSounds();
    void Stop2DSounds();
    void Stop(Tag);
    void Stop(SoundUID);
    void Stop(ObjRef);
    void FadeOut(SoundUID, float duration);

    void AddEmitter(AmbientSoundEmitter&&);
    void UpdateSoundEmitters(float dt);

    namespace Debug {
        inline List<Vector3> Emitters;
    }
}
