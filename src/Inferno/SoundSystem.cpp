#include "pch.h"
#include "DirectX.h"
#include "SoundSystem.h"
#include "FileSystem.h"
#include "Resources.h"
#include "Game.h"
#include "logging.h"
#include "Graphics/Render.h"
#include "Physics.h"
#include "Audio/WAVFileReader.h"
#include "Audio/Audio.h"

//using namespace DirectX;
using namespace DirectX::SimpleMath;
using namespace std::chrono;

namespace Inferno::Sound {
    namespace {
        std::atomic RequestStopSounds = false;
        std::atomic Alive = false;
        std::jthread WorkerThread;
        std::mutex ResetMutex, SoundInstancesMutex, InitMutex;

        constexpr int FREQUENCY_11KHZ = 11025;
        constexpr int FREQUENCY_22KHZ = 22050;

        // Scales game coordinates to audio coordinates.
        // The engine claims to be unitless but doppler, falloff, and reverb are noticeably different using smaller values.
        constexpr float AUDIO_SCALE = 1;
        //constexpr float MAX_SFX_VOLUME = 0.75; // should come from settings
        constexpr float MERGE_WINDOW = 1 / 14.0f; // Merge the same sound being played by a source within a window

        std::condition_variable InitializeCondition;
        IntersectContext Intersect({});
    }

    void WaitInitialized() {
        if (Alive) return;
        std::unique_lock lock(InitMutex);
        auto result = InitializeCondition.wait_until(lock, system_clock::now() + 2s);
        if (result == std::cv_status::timeout)
            SPDLOG_ERROR("Timed out waiting for sound system to initialize");
    }

    struct Sound3DInstance : Sound3D {
        Vector3 Position; // Position the sound comes from
        SegID Segment = SegID::None; // Segment the sound starts in, needed for occlusion
        SideID Side = SideID::None; // Side, used for turning off forcefields
        ObjRef Source = GLOBAL_SOUND_SOURCE; // Source to attach the sound to
        bool FromPlayer = false; // For the player's firing sounds, afterburner, etc. Simulates 2D playback by positioning sound on the listener.
        SoundUID ID = SoundUID::None;

        float Muffle = 1, TargetMuffle = 1;
        bool Started = false;
        Ptr<SoundEffectInstance> Instance;
        AudioEmitter Emitter; // Stores position
        double StartTime = 0;
        bool Alive = false;

        bool IsAlive() const { return Alive; }

        void UpdateEmitter(const Vector3& listener, float dt) {
            if (!Alive) {
                Instance->Stop();
                return;
            }

            if (Source != GLOBAL_SOUND_SOURCE) {
                auto obj = Game::Level.TryGetObject(Source);
                if (obj && obj->IsAlive() /*&& AttachToSource*/) {
                    // Move the emitter to the object location if attached
                    auto pos = obj->GetPosition(Game::LerpAmount);
                    if (AttachOffset != Vector3::Zero) {
                        auto rot = obj->GetRotation(Game::LerpAmount);
                        pos += Vector3::Transform(AttachOffset, rot);
                    }

                    Emitter.SetPosition(pos * AUDIO_SCALE);
                    Segment = obj->Segment;
                }
                else {
                    // Source object is dead, stop the sound
                    Instance->Stop();
                    return;
                }
            }

            assert(Radius > 0);
            auto emitterPos = Emitter.Position / AUDIO_SCALE;
            auto delta = listener - emitterPos;
            Vector3 dir;
            delta.Normalize(dir);
            auto dist = delta.Length();

            //auto ratio = std::min(dist / Radius, 1.0f);
            // 1 / (0.97 + 3x)^2 - 0.065 inverse square that crosses at 0,1 and 1,0
            //auto volume = 1 / std::powf(0.97 + 3*ratio, 2) - 0.065f;

            TargetMuffle = 1; // don't hit test very close sounds

            if (dist < Radius && !RequestStopSounds) {
                // only hit test if sound is actually within range
                if (Looped && !Instance->GetState() == SoundState::PLAYING) {
                    //fmt::print("Starting looped sound\n");
                    SoundLoopInfo info{
                        .LoopBegin = LoopStart,
                        .LoopLength = LoopEnd - LoopStart,
                        .LoopCount = LoopCount <= 0 ? XAUDIO2_LOOP_INFINITE : std::clamp(LoopCount, 1u, (uint)XAUDIO2_MAX_LOOP_COUNT)
                    };

                    Instance->Play(&info);
                }

                if (Occlusion) {
                    constexpr float MUFFLE_MAX = 0.95f;
                    constexpr float MUFFLE_MIN = 0.25f;

                    if (dist > 10) {
                        // don't hit test nearby sounds
                        Ray ray(emitterPos, dir);
                        LevelHit hit;
                        RayQuery query{ .MaxDistance = dist, .Start = Segment };

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
                if ((Looped && Instance->GetState() == SoundState::PLAYING) || RequestStopSounds) {
                    //fmt::print("Stopping out of range looped sound\n");
                    Instance->Stop();
                }
            }

            auto diff = TargetMuffle - Muffle;
            auto sign = Sign(diff);
            Muffle += std::min(abs(diff), dt * 3) * sign; // Take 1/3 a second to reach muffle target

            //auto falloff = std::powf(1 - ratio, 3); // cubic falloff
            //auto falloff = 1 - ratio; // linear falloff
            //auto falloff = 1 - (ratio * ratio); // square falloff
            Instance->SetVolume(Volume /** falloff*/ * Muffle);

            Debug::Emitters.push_back(Emitter.Position / AUDIO_SCALE);
        }
    };

    namespace {
        List<Tag> StopSoundTags;
        List<SoundUID> StopSoundUIDs;
        List<ObjRef> StopSoundSources;

        DataPool<AmbientSoundEmitter> Emitters = { AmbientSoundEmitter::IsAlive, 10 };

        // https://github.com/microsoft/DirectXTK/wiki/AudioEngine
        Ptr<AudioEngine> Engine;
        List<Ptr<SoundEffect>> SoundsD1, SoundsD2;
        Dictionary<string, Ptr<SoundEffect>> SoundsD3;
        DataPool<Sound3DInstance> SoundInstances(&Sound3DInstance::IsAlive, 50);

        AudioListener Listener;

        constexpr X3DAUDIO_CONE c_listenerCone = {
            X3DAUDIO_PI * 5.0f / 6.0f, X3DAUDIO_PI * 11.0f / 6.0f, 1.0f, 0.75f, 0.0f, 0.25f, 0.708f, 1.0f
        };

        constexpr X3DAUDIO_CONE c_emitterCone = {
            0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f
        };
    }

    bool ShouldDispose(const Sound3DInstance& sound) {
        if (RequestStopSounds) return true;

        for (auto& tag : StopSoundTags) {
            if (sound.Segment == tag.Segment && sound.Side == tag.Side)
                return true;
        }

        for (auto& id : StopSoundUIDs) {
            if (sound.ID == id)
                return true;
        }

        for (auto& id : StopSoundSources) {
            if (sound.Source == id)
                return true;
        }

        return false;
    }

    void SoundWorker(milliseconds pollRate) {
        SPDLOG_INFO("Starting audio mixer thread");

        auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result))
            SPDLOG_WARN("CoInitializeEx did not succeed");

        try {
            auto devices = AudioEngine::GetRendererDetails();
            wstring info = L"Available sound devices:\n";
            for (auto& device : devices)
                info += fmt::format(L"{}\n", device.description/*, device.deviceId*/);

            SPDLOG_INFO(L"{}", info);

            auto flags = AudioEngine_EnvironmentalReverb | AudioEngine_ReverbUseFilters | AudioEngine_UseMasteringLimiter;
#ifdef _DEBUG
            flags |= AudioEngine_Debug;
#endif
            Engine = MakePtr<AudioEngine>(flags, nullptr/*, devices[0].deviceId.c_str()*/);
            Engine->SetDefaultSampleRate(22050); // Change based on D1/D2
            SoundsD1.resize(255);
            SoundsD2.resize(255);
            Alive = true;
            InitializeCondition.notify_all();
            SPDLOG_INFO("Sound system initialized");
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Unable to start sound system: {}", e.what());
            return;
        }

        Engine->SetMasterVolume(Settings::Inferno.MasterVolume);

        while (Alive) {
            Debug::Emitters.clear();

            if (Engine->Update()) {
                try {
                    auto dt = (float)pollRate.count() / 1000.0f;
                    //Listener.Update(Render::Camera.Position * AUDIO_SCALE, Render::Camera.Up, dt);
                    Listener.SetOrientation(Render::Camera.GetForward(), Render::Camera.Up);
                    Listener.Position = Render::Camera.Position * AUDIO_SCALE;
                    //Listener.Position = {};
                    //Listener.OrientTop = {};
                    //Listener.OrientTop.y = sin(Game::ElapsedTime * 3.14f);
                    //Listener.OrientTop.x = -cos(Game::ElapsedTime * 3.14f);
                    //Listener.Velocity = {};

                    std::scoped_lock lock(SoundInstancesMutex);
                    for (auto& sound : SoundInstances) {
                        if (sound.Delay > 0) {
                            sound.Delay -= dt;
                            continue;
                        }

                        if (!sound.Alive) continue;
                        auto state = sound.Instance->GetState();

                        sound.Alive = !ShouldDispose(sound);
                        if (!sound.Looped && state == SoundState::STOPPED) {
                            if (sound.Started) {
                                sound.Alive = false; // a one-shot sound finished playing
                            }
                            else {
                                // Check if the source is dead before playing
                                if (sound.Source != GLOBAL_SOUND_SOURCE) {
                                    auto obj = Game::Level.TryGetObject(sound.Source);
                                    if (!obj || !obj->IsAlive()) {
                                        sound.Alive = false;
                                        continue;
                                    }
                                }

                                // New sound
                                sound.Instance->Play();
                                sound.Started = true;
                            }
                        }

                        sound.UpdateEmitter(Render::Camera.Position, dt);
                        // Hack to force sounds caused by the player to be exactly on top of the listener.
                        // Objects and the camera are slightly out of sync due to update timing and threading
                        if (Game::GetState() == GameState::Game && sound.FromPlayer)
                            sound.Emitter.Position = Listener.Position;

                        if (sound.Instance)
                            sound.Instance->Apply3D(Listener, sound.Emitter, false);
                    }

                    StopSoundUIDs.clear();
                    StopSoundSources.clear();
                }
                catch (const std::exception& e) {
                    SPDLOG_ERROR("Error in audio worker: {}", e.what());
                }
                RequestStopSounds = false;
                std::this_thread::sleep_for(pollRate);
            }
            else {
                RequestStopSounds = false;

                // https://github.com/microsoft/DirectXTK/wiki/AudioEngine
                if (!Engine->IsAudioDevicePresent()) {}

                if (Engine->IsCriticalError()) {
                    SPDLOG_WARN("Attempting to reset audio engine");
                    Engine->Reset();
                }

                std::this_thread::sleep_for(1000ms);
            }
        }
        SPDLOG_INFO("Stopping audio mixer thread");
        CoUninitialize();
    }

    // Creates a mono PCM sound effect
    SoundEffect CreateSoundEffect(AudioEngine& engine, span<ubyte> raw, uint32 frequency = 22050, float trimStart = 0, float trimEnd = 0) {
        // create a buffer and store wfx at the beginning.
        int trimStartBytes = int((float)frequency * trimStart);
        int trimEndBytes = int((float)frequency * trimEnd);

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
        wfx->nSamplesPerSec = frequency;
        wfx->nAvgBytesPerSec = frequency;
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

    void Shutdown() {
        if (!Alive) return;
        Alive = false;
        Engine->Suspend();
        WorkerThread.join();
    }

    // HWND is not used directly, but indicates the sound system requires a window
    void Init(HWND, milliseconds pollRate) {
        WorkerThread = std::jthread(SoundWorker, pollRate);
        Listener.pCone = (X3DAUDIO_CONE*)&c_listenerCone;
        Intersect = IntersectContext(Game::Level);
    }

    void SetReverb(Reverb reverb) {
        Engine->SetReverb((AUDIO_ENGINE_REVERB)reverb);
    }

    SoundEffect* LoadSoundD1(int id) {
        if (!Seq::inRange(SoundsD1, id)) return nullptr;
        if (SoundsD1[id]) return SoundsD1[int(id)].get();

        std::scoped_lock lock(ResetMutex);

        // Prioritize reading wavs from filesystem
        if (auto info = Seq::tryItem(Resources::SoundsD1.Sounds, id)) {
            const auto paths = {
                fmt::format("data/d1/{}.wav", info->Name),
                fmt::format("data/{}.wav", info->Name)
            };

            for (auto& path : paths) {
                if (filesystem::exists(path)) {
                    auto data = Inferno::File::ReadAllBytes(path);
                    SPDLOG_INFO("Reading D1 sound {} from `{}`", id, path);
                    return (SoundsD1[int(id)] = MakePtr<SoundEffect>(CreateSoundEffectWav(*Engine, data))).get();
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

        auto data = Resources::SoundsD1.Read(id);
        if (data.empty()) return nullptr;
        return (SoundsD1[int(id)] = MakePtr<SoundEffect>(CreateSoundEffect(*Engine, data, FREQUENCY_11KHZ, trimStart, trimEnd))).get();
    }

    SoundEffect* LoadSoundD2(int id) {
        if (!Seq::inRange(SoundsD2, id)) return nullptr;
        if (SoundsD2[id]) return SoundsD2[int(id)].get();

        std::scoped_lock lock(ResetMutex);
        int frequency = FREQUENCY_22KHZ;

        // Prioritize reading wavs from filesystem
        if (auto info = Seq::tryItem(Resources::SoundsD2.Sounds, id)) {
            const auto paths = {
                fmt::format("data/d2/{}.wav", info->Name),
                fmt::format("data/{}.wav", info->Name)
            };

            for (auto& path : paths) {
                if (filesystem::exists(path)) {
                    auto data = Inferno::File::ReadAllBytes(path);
                    SPDLOG_INFO("Reading D2 sound {} from `{}`", id, path);
                    return (SoundsD2[int(id)] = MakePtr<SoundEffect>(CreateSoundEffectWav(*Engine, data))).get();
                }
            }
        }

        // Read sound from game data

        // The Class 1 driller sound was not resampled for D2 and should be a lower frequency
        if (id == 127)
            frequency = FREQUENCY_11KHZ;

        auto data = Resources::SoundsD2.Read(id);
        if (data.empty()) return nullptr;
        return (SoundsD2[int(id)] = MakePtr<SoundEffect>(CreateSoundEffect(*Engine, data, frequency))).get();
    }

    SoundEffect* LoadSoundD3(const string& fileName) {
        if (fileName.empty()) return nullptr;
        if (SoundsD3[fileName]) return SoundsD3[fileName].get();

        std::scoped_lock lock(ResetMutex);
        auto info = Resources::ReadOutrageSoundInfo(fileName);
        if (!info) return nullptr;

        if (auto data = Resources::Descent3Hog.ReadEntry(info->FileName)) {
            return (SoundsD3[fileName] = MakePtr<SoundEffect>(CreateSoundEffectWav(*Engine, *data))).get();
        }
        else {
            return nullptr;
        }
    }

    SoundEffect* LoadSound(const SoundResource& resource) {
        if (!Alive) return nullptr;

        SoundEffect* sound = LoadSoundD3(resource.D3);
        if (!sound) sound = LoadSoundD1(resource.D1);
        if (!sound) sound = LoadSoundD2(resource.D2);
        return sound;
    }

    SoundUID SoundUIDIndex = SoundUID::None;

    SoundUID GetSoundUID() {
        return SoundUIDIndex = SoundUID(int(SoundUIDIndex) + 1);
    }

    void Play2D(const SoundResource& resource, float volume, float pan, float pitch) {
        auto sound = LoadSound(resource);
        if (!sound) return;
        sound->Play(volume, pitch, pan);
    }

    // Specify LFE level distance curve such that it rolls off much sooner than
    // all non-LFE channels, making use of the subwoofer more dramatic.
    static const X3DAUDIO_DISTANCE_CURVE_POINT Emitter_LFE_CurvePoints[3] = { 0.0f, 1.0f, 0.25f, 0.0f, 1.0f, 0.0f };
    static const X3DAUDIO_DISTANCE_CURVE Emitter_LFE_Curve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_LFE_CurvePoints[0], 3 };

    static const X3DAUDIO_DISTANCE_CURVE_POINT Emitter_Reverb_CurvePoints[3] = { 0.0f, 0.5f, 0.75f, 1.0f, 1.0f, 0.0f };
    static const X3DAUDIO_DISTANCE_CURVE Emitter_Reverb_Curve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_Reverb_CurvePoints[0], 3 };

    static constexpr X3DAUDIO_DISTANCE_CURVE_POINT Emitter_SquaredCurvePoints[] = { { 0.0f, 1.0f }, { 0.2f, 0.65f }, { 0.5f, 0.25f }, { 0.75f, 0.06f }, { 1.0f, 0.0f } };
    static constexpr X3DAUDIO_DISTANCE_CURVE Emitter_SquaredCurve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_SquaredCurvePoints[0], _countof(Emitter_SquaredCurvePoints) };

    static constexpr X3DAUDIO_DISTANCE_CURVE_POINT Emitter_InvSquaredCurvePoints[] = { { 0.0f, 1.0f }, { 0.05f, 0.95f }, { 0.2f, 0.337f }, { 0.4f, 0.145f }, { 0.6f, 0.065f }, { 0.8f, 0.024f }, { 1.0f, 0.0f } };
    static constexpr X3DAUDIO_DISTANCE_CURVE Emitter_InvSquaredCurve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_InvSquaredCurvePoints[0], _countof(Emitter_InvSquaredCurvePoints) };

    static constexpr X3DAUDIO_DISTANCE_CURVE_POINT Emitter_CubicPoints[] = { { 0.0f, 1.0f }, { 0.1f, 0.73f }, { 0.2f, 0.5f }, { 0.4f, 0.21f }, { 0.6f, 0.060f }, { 0.7f, 0.026f }, { 0.8f, 0.01f }, { 1.0f, 0.0f } };
    static constexpr X3DAUDIO_DISTANCE_CURVE Emitter_CubicCurve = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_CubicPoints[0], _countof(Emitter_CubicPoints) };


    SoundUID Play(Sound3DInstance&& sound) {
        auto sfx = LoadSound(sound.Resource);
        if (!sfx) return SoundUID::None;

        if (sound.Looped && sound.LoopStart > sound.LoopEnd)
            throw Exception("Sound3D loop start must be <= loop end");

        auto position = sound.Position * AUDIO_SCALE;

        std::scoped_lock lock(SoundInstancesMutex);
        auto currentTime = Inferno::Clock.GetTotalTimeSeconds();

        if (sound.Merge && !sound.Source.IsNull()) {
            // Check if any emitters are already playing this sound from this source
            for (auto& instance : SoundInstances) {
                if (!instance.IsAlive()) continue;
                if (instance.Source == sound.Source &&
                    instance.Resource == sound.Resource &&
                    instance.StartTime + MERGE_WINDOW > currentTime + sound.Delay &&
                    !instance.Looped) {
                    if (instance.Source != GLOBAL_SOUND_SOURCE)
                        //if (instance.AttachToSource && sound.AttachToSource)
                        instance.AttachOffset = (instance.AttachOffset + sound.AttachOffset) / 2;

                    instance.Emitter.Position = (position + instance.Emitter.Position) / 2;
                    instance.Volume += sound.Volume * 0.5f;
                    //fmt::print("Merged sound effect {}\n", sound.Resource.GetID());
                    return instance.ID; // Don't play sounds within the merge window
                }
            }
        }

        sound.ID = GetSoundUID();
        sound.Instance = sfx->CreateInstance(SoundEffectInstance_Use3D | SoundEffectInstance_ReverbUseFilters);
        sound.Instance->SetVolume(std::clamp(sound.Volume, 0.0f, 10.0f));
        sound.Instance->SetPitch(std::clamp(sound.Pitch, -1.0f, 1.0f));

        //s.Emitter.pVolumeCurve = (X3DAUDIO_DISTANCE_CURVE*)&X3DAudioDefault_LinearCurve;
        sound.Emitter.pVolumeCurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_CubicCurve;
        sound.Emitter.pLFECurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_LFE_Curve;
        sound.Emitter.pReverbCurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_Reverb_Curve;
        sound.Emitter.CurveDistanceScaler = sound.Radius;
        sound.Emitter.Position = position;
        sound.Emitter.DopplerScaler = 1.0f;
        sound.Emitter.InnerRadius = sound.Radius / 6;
        sound.Emitter.InnerRadiusAngle = X3DAUDIO_PI / 4.0f;
        sound.Emitter.pCone = (X3DAUDIO_CONE*)&c_emitterCone;
        sound.StartTime = currentTime + sound.Delay;
        sound.Alive = true;
        ASSERT(sound.Segment != SegID::None || sound.FromPlayer);

        SoundInstances.AddBack(std::move(sound));
        return sound.ID;
    }

    SoundUID Play(const Sound3D& sound, const Vector3& position, SegID seg, SideID side) {
        Sound3DInstance instance(sound);
        instance.Segment = seg;
        instance.Position = position;
        instance.Side = side;
        return Play(std::move(instance));
    }

    SoundUID Play(const Sound3D& sound, const Object& source) {
        Sound3DInstance instance(sound);
        instance.Segment = source.Segment;
        instance.Position = source.Position;
        return Play(std::move(instance));
    }

    SoundUID PlayFrom(const Sound3D& sound, const Object& source) {
        Sound3DInstance instance(sound);
        instance.Segment = source.Segment;
        instance.Position = source.Position;
        instance.Source = Game::GetObjectRef(source);
        return Play(std::move(instance));
    }

    SoundUID AtPlayer(const Sound3D& sound) {
        Sound3DInstance instance(sound);
        instance.Source = Game::Player.Reference;
        instance.FromPlayer = true;
        return Play(std::move(instance));
    }

    void Reset() {
        if (!Engine || !Alive) return;
        std::scoped_lock lock(ResetMutex);
        SPDLOG_INFO("Clearing audio cache");
        //SoundsD1.clear(); // unknown if effects must be stopped before releasing
        Stop3DSounds();

        // Sleep caller while the worker thread finishes cleaning up
        while (RequestStopSounds)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

        std::scoped_lock soundLock(SoundInstancesMutex);
        StopSoundTags.clear();
        StopSoundUIDs.clear();
        StopSoundSources.clear();
        Engine->TrimVoicePool();
        Emitters.Clear();
    }

    void PrintStatistics() {
        if (!Engine || !Alive) return;
        auto stats = Engine->GetStatistics();

        SPDLOG_INFO("Audio stats:\nPlaying: {} / {}\nInstances: {}\nVoices {} / {} / {} / {}\n{} audio bytes",
                    stats.playingOneShots, stats.playingInstances,
                    stats.allocatedInstances,
                    stats.allocatedVoices, stats.allocatedVoices3d,
                    stats.allocatedVoicesOneShot, stats.allocatedVoicesIdle,
                    stats.audioBytes);
    }

    void Pause() { Engine->Suspend(); }
    void Resume() { Engine->Resume(); }

    float GetVolume() { return Alive ? Engine->GetMasterVolume() : 0; }

    void SetVolume(float volume) {
        Settings::Inferno.MasterVolume = volume;
        if (Alive) Engine->SetMasterVolume(volume);
    }

    void Stop3DSounds() {
        if (!Alive) return;

        RequestStopSounds = true;
    }

    void Stop2DSounds() {
        //for (auto& effect : SoundEffects) {

        //}
    }

    void Stop(Tag tag) {
        if (!Alive || !tag) return;
        std::scoped_lock lock(SoundInstancesMutex);
        StopSoundTags.push_back(tag);
    }

    void Stop(SoundUID id) {
        if (!Alive || id == SoundUID::None) return;
        std::scoped_lock lock(SoundInstancesMutex);
        StopSoundUIDs.push_back(id);
    }

    void Stop(ObjRef id) {
        if (!Alive) return;
        std::scoped_lock lock(SoundInstancesMutex);
        StopSoundSources.push_back(id);
    }

    void AddEmitter(AmbientSoundEmitter&& e) {
        if (e.Sounds.empty()) {
            SPDLOG_WARN("Tried to add an empty sound emitter");
            return;
        }

        Emitters.Add(e);
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
                    Play(sound, Listener.Position + RandomVector(emitter.Distance), SegID::None);
                }
                else {
                    Play2D(resource, emitter.Volume.GetRandom());
                }
            }
        }
    }
}
