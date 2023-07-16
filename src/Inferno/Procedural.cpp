#include "pch.h"
#include "Procedural.h"

#include <semaphore>

#include "Graphics/GpuResources.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"

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
                auto name = fmt::format(L"ring buffer {}", i);
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

    // Combined command list / allocator / queue for executing commands
    class CommandContext {
        ComPtr<ID3D12GraphicsCommandList> _cmdList;
        ComPtr<ID3D12CommandAllocator> _allocator;
        ComPtr<ID3D12CommandQueue> _queue;
        ComPtr<ID3D12Fence> _fence;
        Microsoft::WRL::Wrappers::Event _fenceEvent;

        uint64 _fenceValue = 1;
        wstring _name;
    public:
        CommandContext(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, wstring name) : _name(std::move(name)) {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.Type = type;
            ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_queue)));
            ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));
            ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&_allocator)));
            ThrowIfFailed(device->CreateCommandList(1, type, _allocator.Get(), nullptr, IID_PPV_ARGS(&_cmdList)));
            ThrowIfFailed(_cmdList->Close());

            ThrowIfFailed(_queue->SetName(_name.c_str()));
            ThrowIfFailed(_allocator->SetName(_name.c_str()));
            ThrowIfFailed(_cmdList->SetName(_name.c_str()));
            ThrowIfFailed(_fence->SetName(_name.c_str()));

            //_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

            _fenceEvent.Attach(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
            if (!_fenceEvent.IsValid())
                throw std::exception("CreateEvent");
        }

        void Reset() const {
            ThrowIfFailed(_allocator->Reset());
            ThrowIfFailed(_cmdList->Reset(_allocator.Get(), nullptr));
            PIXBeginEvent(_cmdList.Get(), PIX_COLOR_DEFAULT, _name.data());
        }

        ID3D12GraphicsCommandList* Get() const { return _cmdList.Get(); }

        void Execute() const {
            PIXEndEvent(_cmdList.Get());
            ThrowIfFailed(_cmdList->Close());
            ID3D12CommandList* ppCommandLists[] = { _cmdList.Get() };
            _queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
        }

        void Wait() {
            const auto fence = _fenceValue;
            ThrowIfFailed(_queue->Signal(_fence.Get(), fence));
            _fenceValue++;

            // Wait until the command list is finished executing
            if (_fence->GetCompletedValue() < fence) {
                ThrowIfFailed(_fence->SetEventOnCompletion(fence, _fenceEvent.Get()));
                WaitForSingleObjectEx(_fenceEvent.Get(), INFINITE, FALSE);
            }
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
        Ptr<CommandContext> _uploadQueue, _copyQueue;
        double _prevTime = 0;
        Worker _worker = { std::bind_front(&ProceduralWorker::Task, this), "Procedural", 1ms };

    public:
        List<Ptr<ProceduralTextureBase>> Procedurals;

        ProceduralWorker() {
            _uploadQueue = MakePtr<CommandContext>(Render::Device, D3D12_COMMAND_LIST_TYPE_COPY, L"Procedural upload queue");
            _copyQueue = MakePtr<CommandContext>(Render::Device, D3D12_COMMAND_LIST_TYPE_DIRECT, L"Procedural copy queue");
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
            _copyQueue->Reset();

            for (auto& proc : Procedurals) {
                proc->CopyToMainThread(_copyQueue->Get());
            }

            _copyQueue->Execute();
            _copyQueue->Wait();
        }

    protected:
        void Task() {
            _uploadQueue->Reset();

            bool didWork = false;
            auto currentTime = Clock.GetTotalTimeSeconds();

            for (auto& proc : Procedurals) {
                if (proc->Update(_uploadQueue->Get(), currentTime)) {
                    didWork = true;
                }
            }

            // Wait for queue to finish uploading
            _uploadQueue->Execute();
            _uploadQueue->Wait();

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
        ProcWorker = MakePtr<ProceduralWorker>();
    }

    void StopProceduralWorker() {
        ProceduralBuffer.reset();
        ProcWorker.reset();
    }
}
