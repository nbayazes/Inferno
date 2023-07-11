#include "pch.h"
#include "Procedural.h"
#include "Graphics/GpuResources.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"

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


    namespace {
        using namespace std::chrono;
        std::jthread ProceduralWorkerThread;
        std::atomic Alive = false;
        //std::condition_variable ProceduralThreadReady;
        //std::mutex ProceduralMutex;
    }


    Ptr<ProceduralTextureBase> CreateProceduralWater(Outrage::TextureInfo& texture, TexID dest);
    Ptr<ProceduralTextureBase> CreateProceduralFire(Outrage::TextureInfo& texture, TexID dest);
    //Dictionary<string, Ptr<ProceduralTextureBase>> Procedurals;
    List<Ptr<ProceduralTextureBase>> Procedurals;
    Ptr<CommandList> UploadQueue, CopyQueue;

    int GetProceduralCount() { return (int)Procedurals.size(); }

    void FreeProceduralTextures() {
        //std::unique_lock lock(ProceduralMutex);
        //ProceduralThreadReady.wait(lock);
        Procedurals.clear();
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

    Outrage::ProceduralInfo* GetProceduralInfo(TexID id) {
        for (auto& p : Procedurals) {
            if (p->ID == id) return &p->Info.Procedural;
        }
        return nullptr;
    }

    void CopyProceduralsToMaterial() {
        CopyQueue->Reset();

        for (auto& proc : Procedurals) {
            proc->CopyToTexture(CopyQueue->Get());
        }

        // Wait for queue to finish copying
        CopyQueue->Execute(true);
    }

    void ProceduralWorker(milliseconds pollRate) {
        Alive = true;

        double prevTime = 0;

        while (Alive) {

            UploadQueue->Reset();

            bool didWork = false;
            auto elapsedTime = Clock.GetTotalTimeSeconds();

            for (auto& proc : Procedurals) {
                if (proc->ShouldUpdate(elapsedTime)) {
                    proc->Update(UploadQueue->Get(), elapsedTime);
                    didWork = true;
                }
            }

            // Wait for queue to finish
            UploadQueue->Execute(true);

            for (auto& proc : Procedurals)
                proc->WriteComplete();

            if (didWork) {
                Debug::ProceduralUpdateRate = elapsedTime - prevTime;
                prevTime = elapsedTime;
            }

            std::this_thread::sleep_for(pollRate);
        }
    }

    void StartProceduralWorker() {
        UploadQueue = MakePtr<CommandList>(Render::Device, D3D12_COMMAND_LIST_TYPE_COPY, L"Procedural upload queue");
        CopyQueue = MakePtr<CommandList>(Render::Device, D3D12_COMMAND_LIST_TYPE_DIRECT, L"Procedural copy queue");

        ProceduralWorkerThread = std::jthread(ProceduralWorker, milliseconds(1));
    }

    void StopProceduralWorker() {
        FreeProceduralTextures();

        if (!Alive) return;
        Alive = false;
        ProceduralWorkerThread.join();
        UploadQueue.reset();
        CopyQueue.reset();
    }

    class Worker {
        std::string _name;
        std::jthread _worker;
    protected:
        std::atomic<bool> _alive;

    public:
        Worker(std::string_view name) : _name(name) {
            SPDLOG_INFO("Starting worker `{}`", _name);
            _alive = true;
            _worker = std::jthread(&Worker::Task, this);
        }

        virtual ~Worker() {
            SPDLOG_INFO("Stopping worker `{}`", _name);
            _alive = false;
            //if (_worker.joinable())
            //    _worker.join();
        }

        Worker(const Worker&) = delete;
        Worker(Worker&&) = delete;
        Worker& operator=(const Worker&) = delete;
        Worker& operator=(Worker&&) = delete;


    protected:
        virtual void Task() = 0;
    };

    class ProceduralWorker : public Worker {
        Ptr<CommandList> _uploadQueue;
    public:
        ProceduralWorker() : Worker("Procedural") { }

    protected:
        void Task() override {
            _alive = true;

            double prevTime = 0;

            while (_alive) {
                UploadQueue->Reset();

                bool didWork = false;
                auto elapsedTime = Clock.GetTotalTimeSeconds();

                for (auto& proc : Procedurals) {
                    if (proc->ShouldUpdate(elapsedTime)) {
                        proc->Update(UploadQueue->Get(), elapsedTime);
                        didWork = true;
                    }
                }

                // Wait for queue to finish
                UploadQueue->Execute(true);

                for (auto& proc : Procedurals)
                    proc->WriteComplete();

                if (didWork) {
                    Debug::ProceduralUpdateRate = elapsedTime - prevTime;
                    prevTime = elapsedTime;
                }

                std::this_thread::sleep_for(milliseconds(1));
            }
        }
    };
}
