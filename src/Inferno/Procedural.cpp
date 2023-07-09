#include "pch.h"
#include "Procedural.h"
#include "Graphics/GpuResources.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"


namespace Inferno {
    Ptr<ProceduralTextureBase> CreateProceduralWater(Outrage::TextureInfo& texture, TexID id);
    Ptr<ProceduralTextureBase> CreateProceduralFire(Outrage::TextureInfo& texture);
    Dictionary<string, Ptr<ProceduralTextureBase>> Procedurals;

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
            // Create synchronization objects and wait until assets have been uploaded to the GPU.

            // Create an event handle to use for frame synchronization.
            _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (_fenceEvent == nullptr)
                ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));

            // Wait for the command list to execute; we are reusing the same command 
            // list in our main loop but for now, we just want to wait for setup to 
            // complete before continuing.
            //WaitForPreviousFrame();

            const UINT64 fence = _fenceValue;
            ThrowIfFailed(_queue->Signal(_fence.Get(), fence));
            _fenceValue++;

            // Wait until the previous frame is finished.
            if (_fence->GetCompletedValue() < fence) {
                ThrowIfFailed(_fence->SetEventOnCompletion(fence, _fenceEvent));
                WaitForSingleObject(_fenceEvent, INFINITE);
            }
        }
    };

    Ptr<CommandList> UploadQueue;

    void FreeProceduralTextures() {
        Procedurals.clear();
        UploadQueue.reset();
    }

    void CreateTestProcedural(Outrage::TextureInfo& texture) {
        if (!Procedurals.contains(texture.Name)) {
            if (texture.IsWaterProcedural())
                Procedurals[texture.Name] = CreateProceduralWater(texture, TexID(1080));
            else
                Procedurals[texture.Name] = CreateProceduralFire(texture);
        }

        auto ltid = Resources::GameData.LevelTexIdx[1080];
        Resources::GameData.TexInfo[(int)ltid].Procedural = true;
    }

    void CopyProceduralToTexture(const string& srcName, TexID destId) {
        auto& material = Render::Materials->Get(destId);
        auto& src = Procedurals[srcName]->Texture;
        if (!src) return;

        // todo: this is only necessary once on level load
        auto destHandle = Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5);
        Render::Device->CopyDescriptorsSimple(1, destHandle, Procedurals[srcName]->Handle.GetCpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void UploadChangedProcedurals() {
        if (!UploadQueue) {
            UploadQueue = MakePtr<CommandList>(Render::Device, D3D12_COMMAND_LIST_TYPE_COPY, L"Procedural upload queue");
            //CopyQueue = MakePtr<CommandList>(Render::Device, D3D12_COMMAND_LIST_TYPE_DIRECT, L"Procedural copy queue");
        }

        UploadQueue->Reset();

        int count = 0;
        for (auto& tex : Procedurals | views::values) {
            tex->Update();
            if (tex->CopyToTexture(UploadQueue->Get()))
                count++;
        }

        UploadQueue->Execute(true);
    }
}
