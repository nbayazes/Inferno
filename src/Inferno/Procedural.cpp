#include "pch.h"
#include "Procedural.h"

#include <semaphore>

#include "Graphics/GpuResources.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"

using namespace std::chrono;

namespace Inferno {
    // Combined command list / allocator / queue for executing commands
    class CommandList {
        ComPtr<ID3D12GraphicsCommandList> _cmdList;
        ComPtr<ID3D12CommandAllocator> _allocator;
        ComPtr<ID3D12CommandQueue> _queue;
        ComPtr<ID3D12Fence> _fence;

        int _fenceValue = 1;
        HANDLE _fenceEvent{};

    public:
        CommandList(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type, const wstring& name) {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.Type = type;
            ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_queue)));
            ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));
            ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&_allocator)));
            ThrowIfFailed(device->CreateCommandList(1, type, _allocator.Get(), nullptr, IID_PPV_ARGS(&_cmdList)));
            ThrowIfFailed(_cmdList->Close());

            ThrowIfFailed(_queue->SetName(name.c_str()));
            ThrowIfFailed(_allocator->SetName(name.c_str()));
            ThrowIfFailed(_cmdList->SetName(name.c_str()));
            ThrowIfFailed(_fence->SetName(name.c_str()));
        }

        void Reset() const {
            ThrowIfFailed(_cmdList->Reset(_allocator.Get(), nullptr));
        }

        ID3D12GraphicsCommandList* Get() const { return _cmdList.Get(); }

        void Execute(bool wait) {
            ThrowIfFailed(_cmdList->Close());
            ID3D12CommandList* ppCommandLists[] = { _cmdList.Get() };
            _queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            if (wait) Wait();
        }

    private:
        void Wait() {
            // Create an event handle to use for frame synchronization.
            _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (_fenceEvent == nullptr)
                ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

            const UINT64 fence = _fenceValue;
            ThrowIfFailed(_queue->Signal(_fence.Get(), fence));
            _fenceValue++;

            // Wait until the command list is finished executing
            if (_fence->GetCompletedValue() < fence) {
                ThrowIfFailed(_fence->SetEventOnCompletion(fence, _fenceEvent));
                WaitForSingleObject(_fenceEvent, INFINITE);
            }
        }
    };

    // Long running worker that executes a task at a given poll rate
    class Worker {
        std::function<void()> _task;
        std::string _name;
        std::jthread _worker;
        milliseconds _pollRate;
        std::atomic<bool> _alive;
        std::binary_semaphore _pause{ 0 };
    public:
        Worker(std::function<void()> task, std::string_view name, milliseconds pollRate = 0ms)
            : _task(std::move(task)), _name(name), _pollRate(pollRate) {
            _worker = std::jthread(&Worker::WorkThread, this);
        }

        virtual ~Worker() {
            SPDLOG_INFO("Destroying worker {}", _name);
            Pause(); // Make sure there's no running work before destroying thread
            _alive = false;
            //if (_worker.joinable())
            //    _worker.join();
        }

        Worker(const Worker&) = delete;
        Worker(Worker&&) = delete;
        Worker& operator=(const Worker&) = delete;
        Worker& operator=(Worker&&) = delete;

        // Pauses execution after current iteration of worker
        void Pause() {
            _pause.acquire();
        }

        void Resume() {
            _pause.release();
        }

    protected:
        void WorkThread() {
            _alive = true;

            SPDLOG_INFO("Starting worker `{}`", _name);
            while (_alive) {
                //std::this_thread::sleep_for(_pollRate);

                try {
                    _pause.acquire(); // check if thread is paused before executing
                    _task();
                    _pause.release();

                    if (_pollRate > 0ms)
                        std::this_thread::sleep_for(_pollRate);
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
        Ptr<CommandList> _uploadQueue, _copyQueue;
        double _prevTime = 0;
        Worker _worker = { std::bind_front(&ProceduralWorker::Task, this), "Procedural", 1ms };

    public:
        ProceduralWorker() {
            _uploadQueue = MakePtr<CommandList>(Render::Device, D3D12_COMMAND_LIST_TYPE_COPY, L"Procedural upload queue");
            _copyQueue = MakePtr<CommandList>(Render::Device, D3D12_COMMAND_LIST_TYPE_DIRECT, L"Procedural copy queue");
            _worker.Resume();
        }

        List<Ptr<ProceduralTextureBase>> Procedurals;

        void FreeTextures() {
            _worker.Pause();
            Procedurals.clear();
            _worker.Resume();
        }

        void AddProcedural(Outrage::TextureInfo& info, TexID dest) {
            if (Seq::exists(Procedurals, [dest](auto& p) { return p->ID == dest; })) {
                SPDLOG_WARN("Procedural texture already exists for texid {}", dest);
                return;
            }

            auto procedural = info.IsWaterProcedural() ? CreateProceduralWater(info, dest) : CreateProceduralFire(info, dest);

            // Update the diffuse texture handle in the material
            auto& material = Render::Materials->Get(dest);
            auto destHandle = Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5);
            Render::Device->CopyDescriptorsSimple(1, destHandle, procedural->Handle.GetCpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            Procedurals.push_back(std::move(procedural));
        }

        void CopyProceduralsToMaterial() const {
            _copyQueue->Reset();

            for (auto& proc : Procedurals) {
                proc->CopyToTexture(_copyQueue->Get());
            }

            // Wait for queue to finish copying
            _copyQueue->Execute(true);
        }

    protected:
        void Task() {
            //_uploadQueue->Reset();

            //bool didWork = false;
            //auto elapsedTime = Clock.GetTotalTimeSeconds();

            //for (auto& proc : Procedurals) {
            //    if (proc->ShouldUpdate(elapsedTime)) {
            //        proc->Update(_uploadQueue->Get(), elapsedTime);
            //        didWork = true;
            //    }
            //}

            //// Wait for queue to finish
            //_uploadQueue->Execute(true);

            //for (auto& proc : Procedurals)
            //    proc->WriteComplete();

            //if (didWork) {
            //    Debug::ProceduralUpdateRate = elapsedTime - _prevTime;
            //    _prevTime = elapsedTime;
            //}
        }
    };


    Ptr<ProceduralWorker> ProcWorker;

    int GetProceduralCount() { return (int)ProcWorker->Procedurals.size(); }

    void FreeProceduralTextures() {
        if (ProcWorker) ProcWorker->FreeTextures();
    }

    void AddProcedural(Outrage::TextureInfo& info, TexID dest) {
        if (ProcWorker) ProcWorker->AddProcedural(info, dest);
    }

    Outrage::ProceduralInfo* GetProceduralInfo(TexID id) {
        if (!ProcWorker) return nullptr;

        for (auto& p : ProcWorker->Procedurals) {
            if (p->ID == id) return &p->Info.Procedural;
        }
        return nullptr;
    }

    void CopyProceduralsToMaterial() {
        if (ProcWorker) ProcWorker->CopyProceduralsToMaterial();
    }

    void StartProceduralWorker() {
        ProcWorker = MakePtr<ProceduralWorker>();
    }

    void StopProceduralWorker() {
        ProcWorker.reset();
    }
}
