#include "pch.h"
#include "DirectX.h"
#include "SoundSystem.h"
#include "FileSystem.h"
#include "Resources.h"
#include "Game.h"
#include "logging.h"
#include "Graphics/Render.h"
#include <list>

using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace std::chrono;

namespace Inferno::Sound {
    // Scales game coordinates to audio
    constexpr float AUDIO_SCALE = 1 / 30.0f; // 1 game unit = ? meters
    constexpr float MAX_DISTANCE = 400;
    constexpr float MAX_SFX_VOLUME = 0.75; // should come from settings

    struct ObjectSound {
        ObjID Source = ObjID::None;
        //float Volume, Pitch;
        bool Started = false;
        Ptr<SoundEffectInstance> Instance;
        AudioEmitter Emitter;

        void UpdateEmitter(const Vector3& listener, float /*dt*/) {
            if (auto obj = Game::Level.TryGetObject(Source)) {
                //Emitter.Update(obj->Position() * AUDIO_SCALE, obj->Transform.Up(), dt);
                Emitter.SetPosition(obj->Position * AUDIO_SCALE);
                auto dist = (obj->Position - listener).Length();
                auto ratio = std::min(dist / MAX_DISTANCE, 1.0f);
                // 1 / (0.97 + 3x)^2 - 0.065 inverse square that crosses at 0,1 and 1,0
                //auto volume = 1 / std::powf(0.97 + 3*ratio, 2) - 0.065f;

                auto volume = std::powf(1 - ratio, 3);
                Instance->SetVolume(volume * MAX_SFX_VOLUME);
            }
        }
    };

    namespace {
        // https://github.com/microsoft/DirectXTK/wiki/AudioEngine
        Ptr<AudioEngine> Audio;
        List<Ptr<SoundEffect>> Sounds;
        std::atomic<bool> Alive = false;
        std::thread WorkerThread;
        std::list<ObjectSound> ObjectSounds;
        std::mutex ResetMutex, ObjectSoundsMutex;

        AudioListener Listener;

        constexpr X3DAUDIO_CONE c_listenerCone = {
            X3DAUDIO_PI * 5.0f / 6.0f, X3DAUDIO_PI * 11.0f / 6.0f, 1.0f, 0.75f, 0.0f, 0.25f, 0.708f, 1.0f
        };
        constexpr X3DAUDIO_CONE c_emitterCone = {
            0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f
        };

        constexpr X3DAUDIO_DISTANCE_CURVE_POINT c_emitter_LFE_CurvePoints[3] = {
            { 0.0f, 0.1f }, { 0.5f, 0.5f}, { 0.5f, 0.5f }
        };

        constexpr X3DAUDIO_DISTANCE_CURVE c_emitter_LFE_Curve = {
            (X3DAUDIO_DISTANCE_CURVE_POINT*)&c_emitter_LFE_CurvePoints[0], 3
        };

        constexpr X3DAUDIO_DISTANCE_CURVE_POINT c_emitter_Reverb_CurvePoints[3] = {
            { 0.0f, 0.5f}, { 0.75f, 1.0f }, { 1.0f, 0.65f }
        };
        constexpr X3DAUDIO_DISTANCE_CURVE c_emitter_Reverb_Curve = {
            (X3DAUDIO_DISTANCE_CURVE_POINT*)&c_emitter_Reverb_CurvePoints[0], 3
        };
    }

    void SoundWorker(milliseconds pollRate) {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        SPDLOG_INFO("Starting audio mixer thread");
        while (Alive) {
            // Update should be called often, usually in a per - frame update.
            // This can be done on the main rendering thread, or from a worker thread.
            if (Audio->Update()) {
                try {
                    auto dt = pollRate.count() / 1000.0f;
                    //Listener.Update(Render::Camera.Position * AUDIO_SCALE, Render::Camera.Up, dt);
                    Listener.SetOrientation(Render::Camera.GetForward(), Render::Camera.Up);
                    Listener.Position = Render::Camera.Position * AUDIO_SCALE;

                    std::scoped_lock lock(ObjectSoundsMutex);
                    auto sound = ObjectSounds.begin();
                    while (sound != ObjectSounds.end()) {
                        auto state = sound->Instance->GetState();
                        if (state == SoundState::STOPPED && sound->Started) {
                            // clean up
                            SPDLOG_INFO("Removing object sound instance");
                            ObjectSounds.erase(sound++);
                            continue;
                        }

                        if (state == SoundState::STOPPED && !sound->Started) {
                            // New sound
                            sound->Instance->Play();
                            //if (!sound.Loop)
                            sound->Started = true;
                        }

                        sound->UpdateEmitter(Render::Camera.Position, dt);
                        sound->Instance->Apply3D(Listener, sound->Emitter, false);
                        sound++;
                    }
                }
                catch (const std::exception& e) {
                    SPDLOG_ERROR("Error in audio worker: {}", e.what());
                }
            }
            else {
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
        CoUninitialize();
    }

    // Creates a mono PCM sound effect
    SoundEffect CreateSoundEffect(AudioEngine& engine, span<ubyte> raw, uint32 frequency = 22050) {
        // create a buffer and store the waveform info at the beginning.
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
    }

    void Shutdown() {
        if (!Alive) return;
        Alive = false;
        Audio->Suspend();
        WorkerThread.join();
    }

    void Init(HWND, milliseconds pollRate) {
        // HWND is not used, but indicates the sound system needs a window
        auto flags = AudioEngine_EnvironmentalReverb | AudioEngine_ReverbUseFilters;
#ifdef _DEBUG
        flags |= AudioEngine_Debug;
#endif
        Audio = MakePtr<AudioEngine>(flags);
        Sounds.resize(Resources::GetSoundCount());
        SPDLOG_INFO("Init sound system. Sounds: {}", Sounds.size());
        Alive = true;
        WorkerThread = std::thread(SoundWorker, pollRate);

        // no idea what the units on this are, but want to prevent blowing out
        // due to unexpected mixing
        Audio->SetMasteringLimit(5, 600);

        //DWORD channelMask{};
        //Audio->GetMasterVoice()->GetChannelMask(&channelMask);
        //auto hresult = X3DAudioInitialize(channelMask, 20, Audio->Get3DHandle());

        //XAUDIO2_VOICE_DETAILS details{};
        //Audio->GetMasterVoice()->GetVoiceDetails(&details);

        //DSPMatrix.resize(details.InputChannels);
        //DSPSettings.SrcChannelCount = 1;
        //DSPSettings.DstChannelCount = DSPMatrix.size();
        //DSPSettings.pMatrixCoefficients = DSPMatrix.data();

        //X3DAudioCalculate(instance, listener, emitter, flags, &dsp);
    }

    void SetReverb(Reverb reverb) {
        Audio->SetReverb((AUDIO_ENGINE_REVERB)reverb);
    }

    void LoadSound(SoundID id) {
        if (Sounds[int(id)]) return;

        std::scoped_lock lock(ResetMutex);
        int frequency = 22050;

        // Use lower frequency for D1 and the Class 1 driller sound in D2.
        // The Class 1 driller sound was not resampled for D2.
        if ((Game::Level.IsDescent1()) || (int)id == 127)
            frequency = 11025;

        auto data = Resources::ReadSound(id);
        Sounds[int(id)] = MakePtr<SoundEffect>(CreateSoundEffect(*Audio, data, frequency));
    }

    void Play(SoundID id, float volume, float pan, float pitch) {
        LoadSound(id);
        SPDLOG_INFO("Playing sound effect {}", (int)id);
        auto sound = Sounds[int(id)].get();
        sound->Play(volume, pitch, pan);
    }

    void Play3D(SoundID id, float volume, ObjID source, float pitch) {
        LoadSound(id);
        SPDLOG_INFO("Playing sound effect {}", (int)id);
        auto sound = Sounds[int(id)].get();

        {
            std::scoped_lock lock(ObjectSoundsMutex);
            auto& s = ObjectSounds.emplace_back();
            s.Source = source;
            s.Instance = sound->CreateInstance(SoundEffectInstance_Use3D | SoundEffectInstance_ReverbUseFilters);
            s.Instance->SetVolume(volume);
            s.Instance->SetPitch(pitch);

            s.Emitter.pLFECurve = (X3DAUDIO_DISTANCE_CURVE*)&c_emitter_LFE_Curve;
            s.Emitter.pReverbCurve = (X3DAUDIO_DISTANCE_CURVE*)&c_emitter_Reverb_Curve;
            s.Emitter.CurveDistanceScaler = 1.0f;
            //s.Emitter.pCone = (X3DAUDIO_CONE*)&c_emitterCone;
        }
    }

    void ClearCache() {
        std::scoped_lock lock(ResetMutex);

        for (auto& sound : Sounds)
            sound.release(); // unknown if effects must be stopped before releasing
    }
}