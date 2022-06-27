#include "pch.h"
#include "DirectX.h"
#include "SoundSystem.h"
#include "FileSystem.h"
#include "Resources.h"
#include "Game.h"
#include "logging.h"

using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace std::chrono;

namespace Inferno::Sound {
    namespace {
        // https://github.com/microsoft/DirectXTK/wiki/AudioEngine
        Ptr<AudioEngine> Audio;
        List<Ptr<SoundEffect>> Sounds;
        std::atomic<bool> Alive = false;
        std::thread WorkerThread;
        std::mutex ResetMutex;
    }

    void SoundWorker(milliseconds pollRate) {
        SPDLOG_INFO("Starting audio mixer thread");
        while (Alive) {
            // Update should be called often, usually in a per - frame update.
            // This can be done on the main rendering thread, or from a worker thread.
            //
            // This returns false if the audio engine is the 'silent' mode.
            if (!Audio->Update()) {
                if (!Audio->IsAudioDevicePresent()) {
                    // we are in 'silent mode'.
                }
                // attempt recovery
                if (Audio->IsCriticalError()) {
                    // No audio device is active

                }
            }
            std::this_thread::sleep_for(pollRate);
        }
        SPDLOG_INFO("Stopping audio mixer thread");
    }

    // Creates a 22.05 khz mono PCM sound effect
    SoundEffect CreateSoundEffect(AudioEngine& engine, span<ubyte> raw, uint32 frequency = 22050) {
        // create a buffer and store the waveform info at the beginning.
        //Ptr<uint8[]> wavData(new uint8[raw.size() + sizeof(WAVEFORMATEX)]);
        auto wavData = MakePtr<uint8[]>(raw.size() + sizeof(WAVEFORMATEX));
        auto startAudio = wavData.get() + sizeof(WAVEFORMATEX);
        memcpy(startAudio, raw.data(), raw.size());

        auto wfx = (WAVEFORMATEX*)wavData.get();
        wfx->wFormatTag = WAVE_FORMAT_PCM;
        wfx->nChannels = 1;
        wfx->nSamplesPerSec = frequency;
        wfx->nAvgBytesPerSec = frequency;
        wfx->nBlockAlign = 1;
        wfx->wBitsPerSample = 8;
        wfx->cbSize = 0;

        // Pass the ownership of the buffer to the sound effect
        return SoundEffect(&engine, wavData, wfx, startAudio, raw.size());
        //auto sei = SoundEffectInstance(&engine, waveBankEffect, index, flags);
    }

    void Shutdown() {
        if (!Alive) return;
        Alive = false;
        Audio->Suspend();
        WorkerThread.join();
    }

    void Init(HWND, milliseconds pollRate) {
        // HWND is not used, but indicates the sound system needs a window
        auto flags = AudioEngine_Default;
#ifdef _DEBUG
        flags |= AudioEngine_Debug;
#endif
        Audio = MakePtr<AudioEngine>(flags);
        //Audio->SetReverb(Reverb_Underwater);
        Sounds.resize(Resources::GetSoundCount());
        SPDLOG_INFO("Init sound system. Sounds: {}", Sounds.size());
        Alive = true;
        WorkerThread = std::thread(SoundWorker, pollRate);

        /*Seq::iteri(Resources::GetSoundNames(), [](auto i, auto name) {
            SPDLOG_INFO("{}: {}", i, name);
        });*/
    }

    void Play(SoundID id, float volume, float pitch, float pan) {
        std::scoped_lock lock(ResetMutex);
        int frequency = 22050;

        // Lower frequency for D1 and the Class 1 driller sound in D2.
        // The Class 1 driller sound was not resampled for D2.
        if ((Game::Level.IsDescent1()) || (int)id == 127)
            frequency = 11025; 

        if (!Sounds[int(id)]) {
            auto data = Resources::ReadSound(id);
            auto soundEffect = CreateSoundEffect(*Audio, data, frequency);
            Sounds[int(id)] = MakePtr<SoundEffect>(std::move(soundEffect));
        }

        SPDLOG_INFO("Playing sound effect {}", (int)id);
        auto sound = Sounds[int(id)].get();
        sound->Play(volume, pitch, pan);
        // Instead of play, should call CreateInstance() so start/stop and other options are available.
        //auto instance = sound->CreateInstance();
    }

    void ClearCache() {
        std::scoped_lock lock(ResetMutex);
        
        for (auto& sound : Sounds)
            sound.release(); // unknown if effects must be stopped before releasing
    }

    //void PlayPositional(SoundID id, float volume, float pitch, Vector3 source, Vector3 listener) {

    //}

}