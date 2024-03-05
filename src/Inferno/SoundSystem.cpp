#include "pch.h"
#include "SoundSystem.h"
#include "Audio/Audio.h"
#include "Audio/Music.h"
#include "Audio/WAVFileReader.h"
#include "DirectX.h"
#include "FileSystem.h"
#include "Game.h"
#include "Graphics/Render.h"
#include "logging.h"
#include "Physics.h"
#include "Resources.h"

using namespace DirectX::SimpleMath;
using namespace std::chrono;

namespace Inferno::Sound {
    namespace {
        // Scales game coordinates to audio coordinates.
        // The engine claims to be unitless but doppler, falloff, and reverb are noticeably different using smaller values.
        constexpr float AUDIO_SCALE = 1;
        IntersectContext Intersect({});

        constexpr int SAMPLE_RATE_11KHZ = 11025;
        constexpr int SAMPLE_RATE_22KHZ = 22050;

        constexpr float MERGE_WINDOW = 1 / 14.0f; // Merge the same sound being played by a source within a window

        DataPool<AmbientSoundEmitter> Emitters = { AmbientSoundEmitter::IsAlive, 10 };


        constexpr X3DAUDIO_CONE LISTENER_CONE = {
            X3DAUDIO_PI * 5.0f / 6.0f, X3DAUDIO_PI * 11.0f / 6.0f, 1.0f, 0.75f, 0.0f, 0.25f, 0.708f, 1.0f
        };

        constexpr X3DAUDIO_CONE EMITTER_CONE = {
            0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f
        };

        // Specify LFE level distance curve such that it rolls off much sooner than
        // all non-LFE channels, making use of the subwoofer more dramatic.
        constexpr X3DAUDIO_DISTANCE_CURVE_POINT Emitter_LFE_CurvePoints[3] = { 0.0f, 1.0f, 0.25f, 0.0f, 1.0f, 0.0f };
        constexpr X3DAUDIO_DISTANCE_CURVE Emitter_LFE_Curve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_LFE_CurvePoints[0], 3 };

        constexpr X3DAUDIO_DISTANCE_CURVE_POINT Emitter_Reverb_CurvePoints[3] = { 0.0f, 0.5f, 0.75f, 1.0f, 1.0f, 0.0f };
        constexpr X3DAUDIO_DISTANCE_CURVE Emitter_Reverb_Curve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_Reverb_CurvePoints[0], 3 };

        //constexpr X3DAUDIO_DISTANCE_CURVE_POINT Emitter_SquaredCurvePoints[] = { { 0.0f, 1.0f }, { 0.2f, 0.65f }, { 0.5f, 0.25f }, { 0.75f, 0.06f }, { 1.0f, 0.0f } };
        //constexpr X3DAUDIO_DISTANCE_CURVE Emitter_SquaredCurve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_SquaredCurvePoints[0], _countof(Emitter_SquaredCurvePoints) };

        //constexpr X3DAUDIO_DISTANCE_CURVE_POINT Emitter_InvSquaredCurvePoints[] = { { 0.0f, 1.0f }, { 0.05f, 0.95f }, { 0.2f, 0.337f }, { 0.4f, 0.145f }, { 0.6f, 0.065f }, { 0.8f, 0.024f }, { 1.0f, 0.0f } };
        //constexpr X3DAUDIO_DISTANCE_CURVE Emitter_InvSquaredCurve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_InvSquaredCurvePoints[0], _countof(Emitter_InvSquaredCurvePoints) };

        constexpr X3DAUDIO_DISTANCE_CURVE_POINT Emitter_CubicPoints[] = { { 0.0f, 1.0f }, { 0.1f, 0.73f }, { 0.2f, 0.5f }, { 0.4f, 0.21f }, { 0.6f, 0.060f }, { 0.7f, 0.026f }, { 0.8f, 0.01f }, { 1.0f, 0.0f } };
        constexpr X3DAUDIO_DISTANCE_CURVE Emitter_CubicCurve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_CubicPoints[0], _countof(Emitter_CubicPoints) };
    }

    Ptr<MusicStream> CreateMusicStream(List<byte>&& data);

    struct PlayMusicInfo {
        string file;
        List<byte> data;
        bool loop = true;
    };

    struct PlaySound2DInfo {
        SoundResource resource;
        float volume = 1;
        float pan = 0;
        float pitch = 0;
    };

    struct PlaySound3DInfo {
        Sound3D Sound;
        Vector3 Position; // Position the sound comes from
        SegID Segment = SegID::None; // Segment the sound starts in, needed for occlusion
        SideID Side = SideID::None; // Side, used for turning off forcefields
        ObjRef Source = GLOBAL_SOUND_SOURCE; // Source to attach the sound to
        bool FromPlayer = false; // For the player's firing sounds, afterburner, etc. Simulates 2D playback by positioning sound on the listener.
        SoundUID ID = SoundUID::None;
    };

    struct Sound3DInstance {
        PlaySound3DInfo Info;
        float Delay = 0; // Delay before playing

        float Muffle = 1, TargetMuffle = 1;
        bool Started = false;
        Ptr<SoundEffectInstance> Effect;
        AudioEmitter Emitter; // Stores position
        double StartTime = 0;
        bool Alive = false;
        int PlayCount = 0;

        bool IsAlive() const { return Alive; }

        void UpdateEmitter(const Vector3& listener, float dt) {
            auto& sound = Info.Sound;

            if (Info.Source != GLOBAL_SOUND_SOURCE) {
                auto obj = Game::Level.TryGetObject(Info.Source);
                if (obj && obj->IsAlive() /*&& AttachToSource*/) {
                    // Move the emitter to the object location if attached
                    auto pos = obj->GetPosition(Game::LerpAmount);
                    if (sound.AttachOffset != Vector3::Zero) {
                        auto rot = obj->GetRotation(Game::LerpAmount);
                        pos += Vector3::Transform(sound.AttachOffset, rot);
                    }

                    Emitter.SetPosition(pos * AUDIO_SCALE);
                    Info.Segment = obj->Segment;
                }
                else {
                    // Source object is dead, stop the sound
                    Effect->Stop();
                    return;
                }
            }

            assert(sound.Radius > 0);
            auto emitterPos = Emitter.Position / AUDIO_SCALE;
            auto delta = listener - emitterPos;
            Vector3 dir;
            delta.Normalize(dir);
            auto dist = delta.Length();

            //auto ratio = std::min(dist / Radius, 1.0f);
            // 1 / (0.97 + 3x)^2 - 0.065 inverse square that crosses at 0,1 and 1,0
            //auto volume = 1 / std::powf(0.97 + 3*ratio, 2) - 0.065f;

            TargetMuffle = 1; // don't hit test very close sounds

            if (dist < sound.Radius) {
                // only hit test if sound is actually within range
                if (sound.Looped && !Effect->GetState() == SoundState::PLAYING) {
                    //fmt::print("Starting looped sound\n");
                    SoundLoopInfo info{
                        .LoopBegin = sound.LoopStart,
                        .LoopLength = sound.LoopEnd - sound.LoopStart,
                        .LoopCount = sound.LoopCount <= 0 ? XAUDIO2_LOOP_INFINITE : std::clamp(sound.LoopCount, 1u, (uint)XAUDIO2_MAX_LOOP_COUNT)
                    };

                    Effect->Play(&info);
                }

                if (sound.Occlusion) {
                    constexpr float MUFFLE_MAX = 0.95f;
                    constexpr float MUFFLE_MIN = 0.25f;

                    if (dist > 20) {
                        // don't hit test nearby sounds
                        Ray ray(emitterPos, dir);
                        LevelHit hit;
                        RayQuery query{ .MaxDistance = dist, .Start = Info.Segment };

                        if (Intersect.RayLevel(ray, query, hit)) {
                            auto hitDist = (listener - hit.Point).Length();
                            // we hit a wall, muffle it based on the distance from the source
                            // a sound coming immediately around the corner shouldn't get muffled much
                            TargetMuffle = std::clamp(1 - hitDist / 60, MUFFLE_MIN, MUFFLE_MAX);
                        }
                    }
                }
            }
            else {
                // stop looped sounds when going out of range
                if (sound.Looped && Effect->GetState() == SoundState::PLAYING) {
                    //fmt::print("Stopping out of range looped sound\n");
                    Effect->Pause();
                }
            }

            auto diff = TargetMuffle - Muffle;
            auto sign = Sign(diff);
            Muffle += std::min(abs(diff), dt * 3) * sign; // Take 1/3 a second to reach muffle target

            //auto falloff = std::powf(1 - ratio, 3); // cubic falloff
            //auto falloff = 1 - ratio; // linear falloff
            //auto falloff = 1 - (ratio * ratio); // square falloff
            Effect->SetVolume(sound.Volume /** falloff*/ * Muffle);

            Debug::Emitters.push_back(Emitter.Position / AUDIO_SCALE);
        }
    };

    // Creates a mono PCM sound effect
    SoundEffect CreateSoundEffect(AudioEngine& engine, span<ubyte> raw, uint32 sampleRate = SAMPLE_RATE_22KHZ, float trimStart = 0, float trimEnd = 0) {
        // create a buffer and store wfx at the beginning.
        int trimStartBytes = int((float)sampleRate * trimStart);
        int trimEndBytes = int((float)sampleRate * trimEnd);

        // Leave data for the trimmed end in case the sound is looped
        const size_t wavDataSize = raw.size() + sizeof(WAVEFORMATEX) - trimStartBytes;
        auto wavData = MakePtr<uint8[]>(wavDataSize);

        if (trimEnd) {
            for (int i = 0; i < wavDataSize; i++)
                wavData[i] = 128; // constant value is silence
        }

        auto startAudio = wavData.get() + sizeof(WAVEFORMATEX);
        memcpy(startAudio, raw.data() + trimStartBytes, raw.size() - trimStartBytes - trimEndBytes);

        auto wfx = (WAVEFORMATEX*)wavData.get();
        wfx->wFormatTag = WAVE_FORMAT_PCM;
        wfx->nChannels = 1;
        wfx->nSamplesPerSec = sampleRate;
        wfx->nAvgBytesPerSec = sampleRate;
        wfx->nBlockAlign = 1;
        wfx->wBitsPerSample = 8;
        wfx->cbSize = 0;

        // Pass the ownership of the buffer to the sound effect
        return SoundEffect(&engine, wavData, wfx, startAudio, raw.size() - trimStartBytes);
    }

    SoundEffect CreateSoundEffectWav(AudioEngine& engine, span<ubyte> raw) {
        WAVData result{};
        LoadWAVAudioInMemoryEx(raw.data(), raw.size(), result);

        // create a buffer and store wfx at the beginning.
        auto wavData = MakePtr<uint8[]>(result.audioBytes + sizeof(WAVEFORMATEX));
        auto pWavData = wavData.get();
        auto startAudio = pWavData + sizeof(WAVEFORMATEX);
        memcpy(pWavData, result.wfx, sizeof(WAVEFORMATEX));
        memcpy(pWavData + sizeof(WAVEFORMATEX), result.startAudio, result.audioBytes);

        // Pass the ownership of the buffer to the sound effect
        return SoundEffect(&engine, wavData, (WAVEFORMATEX*)wavData.get(), startAudio, result.audioBytes);
    }

    class SoundWorker {
        // https://github.com/microsoft/DirectXTK/wiki/AudioEngine
        Ptr<AudioEngine> _engine;

        SoundFile _soundsD1, _soundsD2;
        List<Ptr<SoundEffect>> _effectsD1, _effectsD2;

        Dictionary<string, Ptr<SoundEffect>> _soundsD3;
        std::condition_variable _initializedCondition;
        std::condition_variable _idleCondition;
        //std::mutex ResetMutex, SoundInstancesMutex, InitMutex;
        std::mutex _threadMutex; // mutex for whenever data is copied between threads
        std::jthread _worker;
        std::stop_token _stopToken;
        milliseconds _pollRate;
        Ptr<MusicStream> _musicStream;
        std::atomic<bool> _requestStopSounds = false, _requestStopMusic = false;

        List<Tag> _stopSoundTags;
        List<SoundUID> _stopSoundUIDs;
        List<ObjRef> _stopSoundSources;

        AudioListener _listener;

        PlayMusicInfo _musicInfo;
        DataPool<Sound3DInstance> _soundInstances{ &Sound3DInstance::IsAlive, 50 };

        List<PlaySound3DInfo> _pending3dSounds;
        List<PlaySound2DInfo> _pending2dSounds;

    public:
        SoundWorker(milliseconds pollRate) : _pollRate(pollRate) {
            _effectsD1.resize(255);
            _effectsD2.resize(255);
            _listener.pCone = (X3DAUDIO_CONE*)&LISTENER_CONE;

            auto flags = AudioEngine_EnvironmentalReverb | AudioEngine_ReverbUseFilters | AudioEngine_UseMasteringLimiter;
#ifdef _DEBUG
            flags |= AudioEngine_Debug;
#endif
            _engine = make_unique<AudioEngine>(flags, nullptr/*, devices[0].deviceId.c_str()*/);

            _worker = std::jthread(&SoundWorker::Task, this);
            _stopToken = _worker.get_stop_token();
        }

        ~SoundWorker() {
            // Join so thread exits before resources are freed from the class
            _worker.request_stop();
            _worker.join();
        }

        SoundWorker(const SoundWorker&) = delete;
        SoundWorker(SoundWorker&&) = delete;
        SoundWorker& operator=(const SoundWorker&) = delete;
        SoundWorker& operator=(SoundWorker&&) = delete;

        AudioEngine* GetEngine() const { return _engine ? _engine.get() : nullptr; }

        std::atomic<bool> RequestUnloadD1 = false;
        std::atomic<bool> ReloadSoundFiles = false;

        void StopAllSounds() {
            _requestStopSounds = true;
            //StopMusic();
            //Stop3DSounds();
        }

        //void WaitInitialize() {
        //    if (Alive) return;
        //    std::unique_lock lock(_threadMutex);
        //    auto result = _initializedCondition.wait_until(lock, system_clock::now() + 2s);
        //    if (result == std::cv_status::timeout)
        //        SPDLOG_ERROR("Timed out waiting for sound worker to initialize");
        //}

        // Waits until the worker thread is idle
        void WaitIdle() {
            std::unique_lock lock(_threadMutex);
            auto result = _idleCondition.wait_until(lock, system_clock::now() + 2s);
            if (result == std::cv_status::timeout)
                SPDLOG_ERROR("Timed out waiting for sound worker to become idle");
        }

        void PlaySound2D(const PlaySound2DInfo& sound) {
            std::unique_lock lock(_threadMutex);
            _pending2dSounds.push_back(sound);
        }

        SoundUID PlaySound3D(PlaySound3DInfo& sound) {
            std::unique_lock lock(_threadMutex);

            sound.ID = GetSoundUID();
            //SPDLOG_INFO("Submit sound {}", (int)sound.ID);
            _pending3dSounds.push_back(sound);
            return sound.ID;
        }

        void StopSound(Tag tag) {
            if (!tag) return;
            std::scoped_lock lock(_threadMutex);
            _stopSoundTags.push_back(tag);
        }

        void StopSound(SoundUID id) {
            if (id == SoundUID::None) return;
            std::scoped_lock lock(_threadMutex);
            _stopSoundUIDs.push_back(id);
        }

        void StopSound(ObjRef source) {
            std::scoped_lock lock(_threadMutex);
            _stopSoundSources.push_back(source);
        }

        void Stop3DSounds() {}
        void Stop2DSounds() {}

        void StopMusic() {
            SPDLOG_INFO("Stopping music");
            _requestStopMusic = true;
        }

        void PlayMusic(const PlayMusicInfo& info) {
            std::scoped_lock lock(_threadMutex);
            _musicInfo = info;
        }

        void SetMusicVolume(float volume) {
            std::scoped_lock lock(_threadMutex);
            if (_musicStream)
                _musicStream->Effect->SetVolume(volume);
        }

        void PrintStatistics() const {
            auto stats = _engine->GetStatistics();

            SPDLOG_INFO("Audio stats:\nPlaying: {} / {}\nInstances: {}\nVoices {} / {} / {} / {}\n{} audio bytes",
                        stats.playingOneShots, stats.playingInstances,
                        stats.allocatedInstances,
                        stats.allocatedVoices, stats.allocatedVoices3d,
                        stats.allocatedVoicesOneShot, stats.allocatedVoicesIdle,
                        stats.audioBytes);
        }

        //void LoadSoundFiles() {
        //    // it is important to snapshot these to prevent threading issues
        //    _soundFileD1 = Resources::SoundsD1;
        //    _soundFileD2 = Resources::SoundsD2;

        //    ReloadSoundFiles = false;
        //}

        void CopySoundIds() {
            SPDLOG_INFO("Copied sound ids");
            std::scoped_lock lock(_threadMutex);
            _soundsD1 = Resources::SoundsD1;
            _soundsD2 = Resources::SoundsD2;
            ReloadSoundFiles = false;
        }

    private:
        SoundUID _soundUid = SoundUID::None;

        SoundUID GetSoundUID() {
            return _soundUid = SoundUID(int(_soundUid) + 1);
        }

        void Initialize() {
            SPDLOG_INFO("Starting audio mixer thread");

            if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
                SPDLOG_WARN("CoInitializeEx did not succeed");

            try {
                auto devices = AudioEngine::GetRendererDetails();
                wstring info = L"Available sound devices:\n";
                for (auto& device : devices)
                    info += fmt::format(L"{}\n", device.description/*, device.deviceId*/);

                SPDLOG_INFO(info);

                _engine->SetMasterVolume(Settings::Inferno.MasterVolume);
                _initializedCondition.notify_all();
                SPDLOG_INFO("Sound system initialized");
            }
            catch (const std::exception& e) {
                SPDLOG_ERROR("Unable to start sound system: {}", e.what());
            }
        }

        bool PlayMusic(string file, bool loop) {
            //StopMusic();

            auto data = Resources::ReadBinaryFile(file);
            if (data.empty()) {
                SPDLOG_WARN("Music file {} not found", file);
                return false;
            }

            if (file.ends_with(".hmp")) {
                SPDLOG_WARN("HMP / MIDI music not implemented!");
                return false;
            }


            _musicStream = CreateMusicStream(std::move(data));
            if (!_musicStream) {
                SPDLOG_WARN("Unable to create music stream from {}", file);
                return false;
            }

            SPDLOG_INFO("Playing music {}", file);
            _musicStream->Loop = loop;
            _musicStream->Effect->SetVolume(Settings::Inferno.MusicVolume);
            _musicStream->Effect->Play();
            return true;
        }

        void PlaySound3DInternal(const PlaySound3DInfo& playInfo) {
            ASSERT(playInfo.Segment != SegID::None || playInfo.FromPlayer);
            auto& sound = playInfo.Sound;

            auto sfx = LoadSound(sound.Resource);
            if (!sfx) return;

            if (sound.Looped && sound.LoopStart > sound.LoopEnd) {
                SPDLOG_ERROR("Sound3D loop start must be <= loop end");
                return;
            }

            if (sound.Merge && !playInfo.Source.IsNull()) {
                //for (auto& pending : _pending3dSounds) {
                //    if (pending.Source == info.Source &&
                //        pending.Sound.Resource == sound.Resource &&
                //        !sound.Looped) {
                //        pending.Sound.Volume += sound.Volume * 0.5f;
                //        SPDLOG_INFO("Discarded pending sound");
                //        return pending.ID; // don't duplicate pending sounds
                //    }
                //}

                uint liveSounds = 0;
                auto currentTime = Inferno::Clock.GetTotalTimeSeconds();

                // Check if any emitters are already playing this sound from this source
                for (auto& inst : _soundInstances) {
                    if (!inst.IsAlive()) continue;
                    auto& info = inst.Info;
                    liveSounds++;
                    if (info.Source == playInfo.Source &&
                        info.Sound.Resource == sound.Resource &&
                        inst.StartTime + MERGE_WINDOW > currentTime + sound.Delay &&
                        !info.Sound.Looped) {
                        if (info.Source != GLOBAL_SOUND_SOURCE)
                            info.Sound.AttachOffset = (sound.AttachOffset + inst.Info.Sound.AttachOffset) / 2;

                        inst.Emitter.Position = (playInfo.Position + inst.Emitter.Position) / 2;
                        info.Sound.Volume += sound.Volume * 0.5f;
                        //SPDLOG_INFO("Discarded sound");
                        //fmt::print("Merged sound effect {}\n", sound.Resource.GetID());
                        return; // Don't play sounds within the merge window
                    }
                }
                //SPDLOG_INFO("Live sounds {}", _soundInstances.Count());
            }

            auto& instance = _soundInstances.Alloc();
            instance.Effect = sfx->CreateInstance(SoundEffectInstance_Use3D | SoundEffectInstance_ReverbUseFilters);
            instance.Effect->SetVolume(std::clamp(sound.Volume, 0.0f, 10.0f));
            instance.Effect->SetPitch(std::clamp(sound.Pitch, -1.0f, 1.0f));

            //s.Emitter.pVolumeCurve = (X3DAUDIO_DISTANCE_CURVE*)&X3DAudioDefault_LinearCurve;
            instance.Emitter.pVolumeCurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_CubicCurve;
            instance.Emitter.pLFECurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_LFE_Curve;
            instance.Emitter.pReverbCurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_Reverb_Curve;
            instance.Emitter.CurveDistanceScaler = sound.Radius;
            instance.Emitter.DopplerScaler = 1.0f;
            instance.Emitter.InnerRadius = sound.Radius / 6;
            instance.Emitter.InnerRadiusAngle = X3DAUDIO_PI / 4.0f;
            instance.Emitter.pCone = (X3DAUDIO_CONE*)&EMITTER_CONE;
            instance.StartTime = Inferno::Clock.GetTotalTimeSeconds() + sound.Delay;
            instance.Info = playInfo;
            instance.Emitter.Position = playInfo.Position;
            instance.Alive = true;
            instance.Delay = sound.Delay;
        }

        void OnStopAllSounds() {
            _stopSoundTags.clear();
            _stopSoundUIDs.clear();
            _stopSoundSources.clear();
            _requestStopSounds = false;
        }

        void OnStopMusic() {
            if (!_musicStream) return;
            _musicStream->Effect->Stop();
            _musicStream = {};
            _requestStopMusic = false;
        }

        void ProcessPending() {
            for (auto& pending : _pending2dSounds) {
                if (auto sound = LoadSound(pending.resource))
                    sound->Play(pending.volume, pending.pitch, pending.pan);
            }

            _pending2dSounds.clear();

            for (auto& pending : _pending3dSounds) {
                //SPDLOG_INFO("Play sound {} : ID {}", pending.Sound.Resource.D1, (int)pending.ID);
                PlaySound3DInternal(pending);
            }

            _pending3dSounds.clear();
        }

        void Update() {
            auto dt = (float)_pollRate.count() / 1000.0f;
            _listener.SetOrientation(Render::Camera.GetForward(), Render::Camera.Up);
            _listener.Position = Render::Camera.Position * AUDIO_SCALE;
            //std::scoped_lock lock(SoundInstancesMutex);
            std::scoped_lock lock(_threadMutex);

            ProcessPending();

            if (!_musicInfo.data.empty()) {
                StopMusic();
                _musicStream = CreateMusicStream(std::move(_musicInfo.data));
                if (_musicStream) {
                    _musicStream->Loop = _musicInfo.loop;
                    _musicStream->Effect->SetVolume(Settings::Inferno.MusicVolume);
                    _musicStream->Effect->Play();
                }
                _musicInfo = {};
            }
            else if (!_musicInfo.file.empty()) {
                PlayMusic(_musicInfo.file, _musicInfo.loop);
                _musicInfo = {};
            }

            for (auto& instance : _soundInstances) {
                if (instance.Delay > 0) {
                    instance.Delay -= dt;
                    continue;
                }

                if (!instance.Alive || !instance.Effect) continue;

                instance.UpdateEmitter(Render::Camera.Position, dt);

                if (instance.PlayCount == 0) {
                    // Check if the source is dead before playing
                    if (instance.Info.Source != GLOBAL_SOUND_SOURCE) {
                        auto obj = Game::Level.TryGetObject(instance.Info.Source);
                        if (!obj || !obj->IsAlive()) {
                            instance.Alive = false;
                            continue;
                        }
                    }

                    instance.Effect->Play();
                    instance.PlayCount++;
                }

                if (!instance.Info.Sound.Looped && instance.Effect->GetState() == SoundState::STOPPED && instance.PlayCount > 0) {
                    instance.Alive = false; // a one-shot sound finished playing
                }

                if (ShouldStop(instance)) {
                    instance.Effect->Stop();
                    instance.Alive = false;
                }

                // Hack to force sounds caused by the player to be exactly on top of the listener.
                // Objects and the camera are slightly out of sync due to update timing and threading
                if (Game::GetState() == GameState::Game && instance.Info.FromPlayer)
                    instance.Emitter.Position = _listener.Position;

                instance.Effect->Apply3D(_listener, instance.Emitter, false);
            }

            if (RequestUnloadD1) {
                SPDLOG_INFO("Unloading D1 sounds");
                for (auto& sound : _soundInstances) {
                    sound.Effect->Stop();
                    sound.Effect = {};
                }

                _engine->TrimVoicePool();

                for (auto& sound : _effectsD1) {
                    if (sound) sound.reset();
                }

                RequestUnloadD1 = false;
            }

            if (_requestStopMusic) OnStopMusic();
            if (_requestStopSounds) OnStopAllSounds();

            _stopSoundUIDs.clear();
            _stopSoundSources.clear();
            _stopSoundTags.clear();
        }

        void Task() {
            Initialize();

            while (!_stopToken.stop_requested()) {
                Debug::Emitters.clear();

                if (_engine->Update()) {
                    try {
                        Update();
                    }
                    catch (const std::exception& e) {
                        SPDLOG_ERROR("Error in audio worker: {}", e.what());
                    }

                    if (_requestStopSounds) {
                        _engine->TrimVoicePool();
                    }

                    _requestStopSounds = false;
                    _requestStopMusic = false;

                    if (!_requestStopSounds) {
                        _idleCondition.notify_all();
                        std::this_thread::sleep_for(_pollRate);
                    }
                }
                else {
                    _requestStopSounds = false;
                    _requestStopMusic = false;

                    // https://github.com/microsoft/DirectXTK/wiki/AudioEngine
                    if (!_engine->IsAudioDevicePresent()) {}

                    if (_engine->IsCriticalError()) {
                        SPDLOG_WARN("Attempting to reset audio engine");
                        _engine->Reset();
                    }

                    _idleCondition.notify_all();
                    std::this_thread::sleep_for(1000ms);
                }
            }

            _musicStream.reset();

            SPDLOG_INFO("Stopping audio mixer thread");
            CoUninitialize();
        }

        bool ShouldStop(const Sound3DInstance& sound) const {
            if (_requestStopSounds) return true;

            for (auto& tag : _stopSoundTags) {
                if (sound.Info.Segment == tag.Segment && sound.Info.Side == tag.Side)
                    return true;
            }

            for (auto& id : _stopSoundUIDs) {
                if (sound.Info.ID == id)
                    return true;
            }

            for (auto& id : _stopSoundSources) {
                if (sound.Info.Source == id)
                    return true;
            }

            return false;
        }

        SoundEffect* LoadSoundD1(int id) {
            if (!Seq::inRange(_effectsD1, id)) return nullptr;
            if (_effectsD1[id]) return _effectsD1[int(id)].get();

            // Prioritize reading wavs from filesystem
            if (auto info = Seq::tryItem(_soundsD1.Sounds, id)) {
                const auto paths = {
                    fmt::format("data/d1/{}.wav", info->Name),
                    fmt::format("data/{}.wav", info->Name)
                };

                for (auto& path : paths) {
                    if (filesystem::exists(path)) {
                        auto data = Inferno::File::ReadAllBytes(path);
                        SPDLOG_INFO("Reading D1 sound {} from `{}`", id, path);
                        return (_effectsD1[int(id)] = MakePtr<SoundEffect>(CreateSoundEffectWav(*_engine, data))).get();
                    }
                }
            }

            // Read sound from game data
            float trimStart = 0;
            if (id == 47)
                trimStart = 0.05f; // Trim the first 50ms from the door close sound due to a popping noise

            float trimEnd = 0;
            if (id == 42)
                trimEnd = 0.05f; // Trim the end of the fan loop due to a pop

            auto data = _soundsD1.Compressed ? _soundsD1.ReadCompressed(id) : _soundsD1.Read(id);
            if (data.empty()) return nullptr;
            return (_effectsD1[int(id)] = MakePtr<SoundEffect>(CreateSoundEffect(*_engine, data, SAMPLE_RATE_11KHZ, trimStart, trimEnd))).get();
        }


        SoundEffect* LoadSoundD2(int id) {
            if (!Seq::inRange(_effectsD2, id)) return nullptr;
            if (_effectsD2[id]) return _effectsD2[int(id)].get();

            int sampleRate = SAMPLE_RATE_22KHZ;

            // Prioritize reading wavs from filesystem
            if (auto info = Seq::tryItem(_soundsD2.Sounds, id)) {
                const auto paths = {
                    fmt::format("data/d2/{}.wav", info->Name),
                    fmt::format("data/{}.wav", info->Name)
                };

                for (auto& path : paths) {
                    if (filesystem::exists(path)) {
                        auto data = Inferno::File::ReadAllBytes(path);
                        SPDLOG_INFO("Reading D2 sound {} from `{}`", id, path);
                        return (_effectsD2[int(id)] = MakePtr<SoundEffect>(CreateSoundEffectWav(*_engine, data))).get();
                    }
                }
            }

            // Read sound from game data

            // The Class 1 driller sound was not resampled for D2 and should have a slower sample rate
            if (id == 127)
                sampleRate = SAMPLE_RATE_11KHZ;

            auto data = _soundsD2.Read(id);
            if (data.empty()) return nullptr;
            return (_effectsD2[int(id)] = MakePtr<SoundEffect>(CreateSoundEffect(*_engine, data, sampleRate))).get();
        }

        SoundEffect* LoadSoundD3(const string& fileName) {
            if (fileName.empty()) return nullptr;
            if (_soundsD3[fileName]) return _soundsD3[fileName].get();

            auto info = Resources::ReadOutrageSoundInfo(fileName);
            if (!info) return nullptr;

            if (auto data = Resources::Descent3Hog.ReadEntry(info->FileName)) {
                return (_soundsD3[fileName] = MakePtr<SoundEffect>(CreateSoundEffectWav(*_engine, *data))).get();
            }
            else {
                return nullptr;
            }
        }

        SoundEffect* LoadSound(const SoundResource& resource) {
            SoundEffect* sound = LoadSoundD3(resource.D3);
            if (!sound) sound = LoadSoundD1(resource.D1);
            if (!sound) sound = LoadSoundD2(resource.D2);
            return sound;
        }
    };

    Ptr<SoundWorker> SoundThread;


    void SetReverb(Reverb reverb) {
        if (GetEngine()) GetEngine()->SetReverb((AUDIO_ENGINE_REVERB)reverb);
        //Engine->SetReverb((AUDIO_ENGINE_REVERB)reverb);
    }

    void Play2D(const SoundResource& resource, float volume, float pan, float pitch) {
        //auto sound = LoadSound(resource);
        //if (!sound) return;
        //sound->Play(volume, pitch, pan);
        if (!SoundThread) return;
        SoundThread->PlaySound2D({ resource, volume, pan, pitch });
    }

    SoundUID Play(const Sound3D& sound, const Vector3& position, SegID seg, SideID side) {
        PlaySound3DInfo info{ sound, position, seg, side };
        return SoundThread->PlaySound3D(info);
    }

    SoundUID Play(const Sound3D& sound, const Object& source) {
        PlaySound3DInfo info{ sound, source.Position, source.Segment };
        return SoundThread->PlaySound3D(info);
    }

    SoundUID PlayFrom(const Sound3D& sound, const Object& source) {
        PlaySound3DInfo info{ sound, source.Position, source.Segment, {}, Game::GetObjectRef(source) };
        return SoundThread->PlaySound3D(info);
    }

    SoundUID AtPlayer(const Sound3D& sound) {
        PlaySound3DInfo info{ .Sound = sound, .Source = Game::Player.Reference, .FromPlayer = true };
        return SoundThread->PlaySound3D(info);
    }

    void StopAllSounds() {
        if (SoundThread) SoundThread->StopAllSounds();
        SoundThread->WaitIdle(); // Block caller until worker thread clears state
    }

    void UnloadD1Sounds() {
        //RequestStopSounds = true;
        //RequestUnloadD1 = true;
        if (SoundThread) SoundThread->RequestUnloadD1 = true;
    }

    void PrintStatistics() {
        if (SoundThread) SoundThread->PrintStatistics();
    }

    void Pause() {
        /*Engine->Suspend();*/
    }

    void Resume() {
        /*Engine->Resume();*/
    }

    float GetVolume() { return SoundThread && SoundThread->GetEngine() ? SoundThread->GetEngine()->GetMasterVolume() : 0; }

    void SetVolume(float volume) {
        Settings::Inferno.MasterVolume = volume;

        if (SoundThread)
            SoundThread->GetEngine()->SetMasterVolume(volume);
    }

    void SetMusicVolume(float volume) {
        Settings::Inferno.MusicVolume = volume;
        if (SoundThread)
            SoundThread->SetMusicVolume(volume);
    }

    void Stop3DSounds() {
        if (SoundThread)
            SoundThread->Stop3DSounds();
    }

    void Stop2DSounds() {
        if (SoundThread)
            SoundThread->Stop2DSounds();
    }

    void Stop(Tag tag) {
        if (SoundThread)
            SoundThread->StopSound(tag);
    }

    void Stop(SoundUID id) {
        if (SoundThread)
            SoundThread->StopSound(id);
    }

    void Stop(ObjRef id) {
        if (SoundThread)
            SoundThread->StopSound(id);
    }

    void AddEmitter(AmbientSoundEmitter&& e) {
        if (e.Sounds.empty()) {
            SPDLOG_WARN("Tried to add an empty sound emitter");
            return;
        }

        Emitters.Add(std::move(e));
    }

    void UpdateSoundEmitters(float dt) {
        for (auto& emitter : Emitters) {
            emitter.Life -= dt;
            if (!AmbientSoundEmitter::IsAlive(emitter)) continue;

            if (Game::Time >= emitter.NextPlayTime) {
                auto index = int(Random() * (emitter.Sounds.size() - 1));
                emitter.NextPlayTime = Game::Time + emitter.Delay.GetRandom();
                SoundResource resource{ emitter.Sounds[index] };

                if (emitter.Distance > 0) {
                    Sound3D sound(resource);
                    sound.Occlusion = false;
                    sound.Volume = emitter.Volume.GetRandom();
                    sound.Radius = emitter.Distance * 3; // Random?
                    // todo: ambient emitters
                    //Play(sound, Listener.Position + RandomVector(emitter.Distance), SegID::None);
                }
                else {
                    Play2D(resource, emitter.Volume.GetRandom());
                }
            }
        }
    }


    inline Ptr<MusicStream> CreateMusicStream(List<byte>&& data) {
        try {
            uint32 fourcc;
            memcpy(&fourcc, data.data(), sizeof(uint32));

            switch (fourcc) {
                case MakeFourCC("OggS"):
                    return std::make_unique<OggStream>(std::move(data));

                case MakeFourCC("RIFF"):
                    //music = LoadWav(bytes);
                    return {};

                case MakeFourCC("fLaC"):
                    return std::make_unique<FlacStream>(std::move(data));

                // MP3 lacks a fourcc
                default:
                    return std::make_unique<Mp3Stream>(std::move(data));
            }
        }
        catch (const Exception& e) {
            SPDLOG_ERROR("Error streaming music: {}", e.what());
            return {};
        }
    }

    bool PlayMusic(List<byte>&& data, bool loop) {
        SoundThread->PlayMusic({ {}, data, loop });

        //CurrentMusicStream = CreateMusicStream(std::move(data));
        //if (!CurrentMusicStream) {
        //    return false;
        //}

        //CurrentMusicStream->Loop = loop;
        //CurrentMusicStream->Effect->SetVolume(Settings::Inferno.MusicVolume);
        //CurrentMusicStream->Effect->Play();
        return true;
    }


    bool PlayMusic(const string& file, bool loop) {
        SoundThread->PlayMusic({ file, {}, loop });
        return true;
    }

    void StopMusic() {
        if (!SoundThread) return;
        SoundThread->StopMusic();
    }

    void Shutdown() {
        SoundThread.reset();
    }

    void WaitInitialized() {
        if (SoundThread) SoundThread->WaitIdle();
    }

    AudioEngine* GetEngine() { return SoundThread ? SoundThread->GetEngine() : nullptr; }

    // HWND is not used directly, but indicates the sound system requires a window
    void Init(HWND, milliseconds pollRate) {
        SoundThread = make_unique<SoundWorker>(pollRate);
        Intersect = IntersectContext(Game::Level);
    }

    void CopySoundIds() {
        if (SoundThread) SoundThread->CopySoundIds();
    }
}
