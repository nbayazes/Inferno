#include "pch.h"

#include <semaphore>
#include "Procedural.h"
#include "SystemClock.h"
#include "Graphics/GpuResources.h"
#include "Graphics/Render.h"
#include "Convert.h"
#include "Resources.h"

using namespace std::chrono;

namespace Inferno {
    template <uint TCapacity>
    class TextureRingBuffer {
        std::atomic<size_t> _index;
        Array<Texture2D, TCapacity> _textures;
        DescriptorRange<1>* _descriptors;

    public:
        TextureRingBuffer(uint resolution, DescriptorRange<1>* descriptors) : _descriptors(descriptors) {
            for (int i = 0; i < std::size(_textures); i++) {
                auto name = fmt::format("ring buffer {}", i);
                auto& texture = _textures[i];
                texture.SetDesc(resolution, resolution);
                texture.CreateOnDefaultHeap(name);
                texture.AddShaderResourceView(descriptors->GetHandle(i));
            }
        }

        Texture2D& GetNext() {
            return _textures[_index++ % TCapacity];
        }
    };


    // Long running worker that executes a task at a given poll rate
    class Worker {
        std::function<void()> _task;
        std::string _name;
        std::jthread _worker;
        milliseconds _pollRate;
        std::atomic<bool> _alive, _paused;
        std::binary_semaphore _pauseWait{ 0 };

    public:
        Worker(std::function<void()> task, string_view name, milliseconds pollRate = 0ms)
            : _task(std::move(task)), _name(name), _pollRate(pollRate) {
            _worker = std::jthread(&Worker::WorkThread, this);
        }

        virtual ~Worker() {
            _alive = false;
            if (_worker.joinable())
                _worker.join();
        }

        Worker(const Worker&) = delete;
        Worker(Worker&&) = delete;
        Worker& operator=(const Worker&) = delete;
        Worker& operator=(Worker&&) = delete;

        // Pauses execution after current iteration of worker
        void Pause(bool shouldPause) {
            if (shouldPause) {
                auto wasPaused = _paused.exchange(true);
                if (!wasPaused)
                    _pauseWait.acquire(); // block caller until thread finishes
            }
            else {
                _paused = false;
                _pauseWait.release();
            }
        }

        bool IsPaused() const { return _paused.load(); }

    protected:
        void WorkThread() {
            _alive = true;

            SPDLOG_INFO("Starting worker `{}`", _name);
            while (_alive) {
                try {
                    if (_paused) {
                        std::this_thread::sleep_for(100ms);
                    }
                    else {
                        // check if thread is paused before executing
                        _pauseWait.acquire();
                        _task();
                        _pauseWait.release();

                        if (_pollRate > 0ms && _alive)
                            std::this_thread::sleep_for(_pollRate);
                    }
                }
                catch (const std::exception& e) {
                    SPDLOG_ERROR(e.what());
                }
            }
            SPDLOG_INFO("Stopping worker `{}`", _name);
        }
    };

    Ptr<ProceduralTextureBase> CreateProceduralWater(Outrage::TextureInfo& texture, TexID dest);
    Ptr<ProceduralTextureBase> CreateProceduralFire(Outrage::TextureInfo& texture, TexID dest);

    class ProceduralWorker {
        Ptr<Inferno::CommandQueue> _uploadQueue, _copyQueue;
        Ptr<CommandContext> _uploadCommands, _copyCommands;
        double _prevTime = 0;
        Worker _worker = { std::bind_front(&ProceduralWorker::Task, this), "Procedural", 1ms };

    public:
        List<Ptr<ProceduralTextureBase>> Procedurals;

        ProceduralWorker(ID3D12Device* device) {
            _uploadQueue = MakePtr<Inferno::CommandQueue>(device, D3D12_COMMAND_LIST_TYPE_COPY, "Procedural Upload Queue");
            _copyQueue = MakePtr<Inferno::CommandQueue>(device, D3D12_COMMAND_LIST_TYPE_DIRECT, "Procedural Copy Queue");
            _uploadCommands = MakePtr<CommandContext>(device, _uploadQueue.get(), "Procedural upload queue");
            _copyCommands = MakePtr<CommandContext>(device, _copyQueue.get(), "Procedural copy queue");
            Pause(false); // Start the worker after creating queues
            Procedurals.reserve(MAX_PROCEDURALS);
        }

        ~ProceduralWorker() {
            FreeTextures();
        }

        bool IsEnabled() const { return !_worker.IsPaused(); }

        void Pause(bool pause) {
            _worker.Pause(pause);
        }

        ProceduralWorker(const ProceduralWorker&) = delete;
        ProceduralWorker(ProceduralWorker&&) = delete;
        ProceduralWorker& operator=(const ProceduralWorker&) = delete;
        ProceduralWorker& operator=(ProceduralWorker&&) = delete;

        void FreeTextures() {
            _worker.Pause(true);
            Procedurals.clear();
            _worker.Pause(false);
        }

        void AddProcedural(Outrage::TextureInfo& info, TexID dest) {
            if (Seq::exists(Procedurals, [dest](auto& p) { return p->ID == dest; })) {
                SPDLOG_WARN("Procedural texture already exists for texid {}", dest);
                return;
            }

            auto procedural = info.IsWaterProcedural() ? CreateProceduralWater(info, dest) : CreateProceduralFire(info, dest);
            Procedurals.push_back(std::move(procedural));
        }

        void CopyProceduralsToMainThread() const {
            if (!IsEnabled()) return;
            _copyCommands->Reset();
            {
                PIXScopedEvent(_copyCommands->GetCommandList(), PIX_COLOR_INDEX(0), "Copy procedurals");

                for (auto& proc : Procedurals) {
                    proc->CopyToMainThread(_copyCommands->GetCommandList());
                }
            }

            _copyCommands->Execute();
            _copyCommands->WaitForIdle();
        }

    protected:
        void Task() {
            _uploadCommands->Reset();

            bool didWork = false;
            auto currentTime = Clock.GetTotalTimeSeconds();

            {
                PIXScopedEvent(_uploadCommands->GetCommandList(), PIX_COLOR_INDEX(1), "Update procedurals");
                for (auto& proc : Procedurals) {
                    if (proc->Update(_uploadCommands->GetCommandList(), currentTime)) {
                        didWork = true;
                    }
                }
            }
            _uploadCommands->Execute();
            _uploadCommands->WaitForIdle();

            for (auto& proc : Procedurals)
                proc->WriteComplete();

            if (didWork) {
                Debug::ProceduralUpdateRate = currentTime - _prevTime;
                _prevTime = currentTime;
            }
        }
    };


    Ptr<ProceduralWorker> ProcWorker;
    Ptr<TextureRingBuffer<MAX_PROCEDURAL_HANDLES>> ProceduralBuffer;

    Texture2D& GetNextTexture() { return ProceduralBuffer->GetNext(); }

    int GetProceduralCount() { return (int)ProcWorker->Procedurals.size(); }

    void EnableProceduralTextures(bool enable) {
        if (ProcWorker) ProcWorker->Pause(!enable);
    }

    void FreeProceduralTextures() {
        if (ProcWorker) ProcWorker->FreeTextures();
    }

    void AddProcedural(Outrage::TextureInfo& info, TexID dest) {
        if (ProcWorker) ProcWorker->AddProcedural(info, dest);
    }

    void EnableProcedural(TexID id, bool enabled) {
        if (id == TexID::None) return;

        if (auto proc = GetProcedural(id)) {
            // Don't enable procedurals for custom textures
            auto& ti = Resources::GetTextureInfo(id);
            proc->Enabled = ti.Custom ? false : enabled;
        }
    }

    ProceduralTextureBase::ProceduralTextureBase(const Outrage::TextureInfo& info, TexID baseTexture) {
        ID = baseTexture;
        Info = info;
        _name = Info.Name;
        _resolution = info.GetSize();
        _resMask = _resolution - 1;
        _totalSize = _resolution * _resolution;
        _pixels.resize(_totalSize);

        for (int i = 0; i < std::size(_textureBuffers); i++) {
            _textureBuffers[i].SetDesc(_resolution, _resolution);
            _textureBuffers[i].CreateOnDefaultHeap(Info.Name + " Buffer");
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ProceduralTextureBase::GetHandle() const {
        if (!_latestTexture)
            return Render::Heaps->Materials.GetGpuHandle((int)ID * Material2D::Count);

        return _latestTexture->GetSRV();
    }

    ProceduralTextureBase* GetProcedural(TexID id) {
        if (!ProcWorker) return nullptr;

        for (auto& p : ProcWorker->Procedurals) {
            if (p->ID == id) return p.get();
        }
        return nullptr;
    }

    void CopyProceduralsToMainThread() {
        if (ProcWorker) ProcWorker->CopyProceduralsToMainThread();
    }

    void StartProceduralWorker() {
        ProceduralBuffer = MakePtr<TextureRingBuffer<MAX_PROCEDURAL_HANDLES>>(128, &Render::Heaps->Procedurals);
        ProcWorker = MakePtr<ProceduralWorker>(Render::Device);
    }

    void StopProceduralWorker() {
        ProceduralBuffer.reset();
        ProcWorker.reset();
    }
}
