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

    Ptr<ProceduralTextureBase> CreateProceduralWater(Outrage::TextureInfo& texture, TexID dest);
    Ptr<ProceduralTextureBase> CreateProceduralFire(Outrage::TextureInfo& texture, TexID dest);
    //Dictionary<string, Ptr<ProceduralTextureBase>> Procedurals;
    List<Ptr<ProceduralTextureBase>> Procedurals;
    Ptr<CommandList> UploadQueue;

    int GetProceduralCount() { return (int)Procedurals.size(); }

    void FreeProceduralTextures() {
        Procedurals.clear();
        UploadQueue.reset();
    }

    void AddProcedural(Outrage::TextureInfo& info, TexID dest) {
        if (Seq::exists(Procedurals, [dest](auto& p) { return p->BaseTexture == dest; })) {
            SPDLOG_WARN("Procedural texture already exists for texid {}", dest);
            return;
        }

        auto procedural = info.IsWaterProcedural() ? CreateProceduralWater(info, dest) : CreateProceduralFire(info, dest);

        auto ltid = Resources::GameData.LevelTexIdx[(int)dest];
        Resources::GameData.TexInfo[(int)ltid].Procedural = true;

        // Update the diffuse texture handle in the material
        auto& material = Render::Materials->Get(dest);
        auto destHandle = Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5);
        Render::Device->CopyDescriptorsSimple(1, destHandle, procedural->Handle.GetCpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        Procedurals.push_back(std::move(procedural));
    }

    void UploadProcedurals() {
        if (!UploadQueue) {
            UploadQueue = MakePtr<CommandList>(Render::Device, D3D12_COMMAND_LIST_TYPE_COPY, L"Procedural upload queue");
        }

        UploadQueue->Reset();

        int count = 0;
        for (auto& tex : Procedurals) {
            tex->Update();
            if (tex->CopyToTexture(UploadQueue->Get()))
                count++;
        }

        UploadQueue->Execute(true);
    }
}
