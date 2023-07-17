#pragma once

#include "Types.h"
#include "DirectX.h"
#include "ShaderLibrary.h"

namespace Inferno::Graphics {
    // Combined command list / allocator / queue for executing commands
    class CommandContext {
        ComPtr<ID3D12Fence> _fence;
        Microsoft::WRL::Wrappers::Event _fenceEvent;
        uint64 _fenceValue = 1;
        wstring _name;

    protected:
        ComPtr<ID3D12GraphicsCommandList> _cmdList;
        ComPtr<ID3D12CommandAllocator> _allocator;
        ComPtr<ID3D12CommandQueue> _localQueue;
        ID3D12CommandQueue* _queue = nullptr;
        std::mutex _eventMutex;

    public:
        CommandContext(ID3D12Device* device, wstring_view name, D3D12_COMMAND_LIST_TYPE type) : _name(name) {
            D3D12_COMMAND_QUEUE_DESC desc = {};
            desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            desc.Type = type;

            ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_localQueue)));
            ThrowIfFailed(_localQueue->SetName(_name.c_str()));
            _queue = _localQueue.Get();

            Initialize(device, type);
        }

        CommandContext(ID3D12Device* device, ID3D12CommandQueue* queue, wstring_view name, D3D12_COMMAND_LIST_TYPE type) : _name(name) {
            _queue = queue;
            Initialize(device, type);
        }

        void Initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type) {
            ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&_allocator)));
            ThrowIfFailed(_allocator->SetName(_name.c_str()));

            ThrowIfFailed(device->CreateCommandList(1, type, _allocator.Get(), nullptr, IID_PPV_ARGS(&_cmdList)));
            ThrowIfFailed(_cmdList->SetName(_name.c_str()));
            ThrowIfFailed(_cmdList->Close()); // Command lists start open

            ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));
            ThrowIfFailed(_fence->SetName(_name.c_str()));

            _fenceEvent.Attach(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
            if (!_fenceEvent.IsValid())
                throw std::exception("CreateEvent");
        }

        virtual ~CommandContext() = default;
        CommandContext(const CommandContext&) = delete;
        CommandContext(CommandContext&&) = delete;
        CommandContext& operator=(const CommandContext&) = delete;
        CommandContext& operator=(CommandContext&&) = delete;

        ID3D12GraphicsCommandList* GetCommandList() const { return _cmdList.Get(); }
        ID3D12CommandQueue* GetCommandQueue() const { return _queue; }

        void BeginEvent(wstring_view name) const {
            PIXBeginEvent(_cmdList.Get(), PIX_COLOR_DEFAULT, name.data());
        }

        void EndEvent() const {
            PIXEndEvent(_cmdList.Get());
        }

        void Reset() const {
            ThrowIfFailed(_allocator->Reset());
            ThrowIfFailed(_cmdList->Reset(_allocator.Get(), nullptr));
        }

        void Execute() {
            ThrowIfFailed(_cmdList->Close());
            ID3D12CommandList* ppCommandLists[] = { _cmdList.Get() };
            _queue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

            ThrowIfFailed(_queue->Signal(_fence.Get(), _fenceValue));
            _fenceValue++;
        }

        // Blocks until command queue finishes execution
        void Wait() {
            if (_fence->GetCompletedValue() < _fenceValue - 1) {
                std::lock_guard lock(_eventMutex); // prevent multiple uses of fence event
                ThrowIfFailed(_fence->SetEventOnCompletion(_fenceValue - 1, _fenceEvent.Get()));
                WaitForSingleObjectEx(_fenceEvent.Get(), INFINITE, FALSE);
            }
        }

        // Waits on another queue
        void InsertWaitForQueue(const CommandContext& other) const {
            ThrowIfFailed(_queue->Wait(other._fence.Get(), other._fenceValue - 1));
        }
    };

    class GraphicsContext : public CommandContext {
    public:
        GraphicsContext(ID3D12Device* device, wstring_view name) : CommandContext(device, name, D3D12_COMMAND_LIST_TYPE_DIRECT) { }
        GraphicsContext(ID3D12Device* device, ID3D12CommandQueue* queue, wstring_view name) : CommandContext(device, queue, name, D3D12_COMMAND_LIST_TYPE_DIRECT) {}

        // Sets multiple render targets with a depth buffer. Used with shaders that write to multiple buffers.
        void SetRenderTargets(span<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs, D3D12_CPU_DESCRIPTOR_HANDLE dsv) const {
            _cmdList->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), false, &dsv);
        }

        // Sets multiple render targets. Used with shaders that write to multiple buffers.
        void SetRenderTargets(span<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs) const {
            _cmdList->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), false, nullptr);
        }

        void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv) const { SetRenderTargets({ &rtv, 1 }); }
        void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv) const { SetRenderTargets({ &rtv, 1 }, dsv); }

        void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) const {
            _cmdList->IASetPrimitiveTopology(topology);
        }

        void ClearColor(RenderTarget& target, const D3D12_RECT* rect = nullptr) const {
            //FlushResourceBarriers();
            target.Transition(_cmdList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
            _cmdList->ClearRenderTargetView(target.GetRTV(), target.ClearColor, (rect == nullptr) ? 0 : 1, rect);
        }

        void ClearColor(ColorBuffer& target, const D3D12_RECT* rect = nullptr) const {
            //FlushResourceBarriers();
            target.Transition(_cmdList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
            _cmdList->ClearRenderTargetView(target.GetRTV(), target.ClearColor, (rect == nullptr) ? 0 : 1, rect);
        }

        void ClearDepth(DepthBuffer& target, const D3D12_RECT* rect = nullptr) const {
            //FlushResourceBarriers();
            target.Transition(_cmdList.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
            _cmdList->ClearDepthStencilView(target.GetDSV(), D3D12_CLEAR_FLAG_DEPTH, target.StencilDepth, 0, (rect == nullptr) ? 0 : 1, rect);
        }

        void SetViewportAndScissor(UINT width, UINT height) const {
            D3D12_RECT scissor{};
            scissor.right = (LONG)width;
            scissor.bottom = (LONG)height;

            D3D12_VIEWPORT viewport{};
            viewport.Width = (float)width;
            viewport.Height = (float)height;
            viewport.MinDepth = D3D12_MIN_DEPTH;
            viewport.MaxDepth = D3D12_MAX_DEPTH;

            _cmdList->RSSetViewports(1, &viewport);
            _cmdList->RSSetScissorRects(1, &scissor);
        }

        template <class T>
        void ApplyEffect(const Effect<T>& effect) {
            //if (ActiveEffect == &effect) return;
            //ActiveEffect = (void*)&effect;
            assert(effect.PipelineState);
            _cmdList->SetPipelineState(effect.PipelineState.Get());
            _cmdList->SetGraphicsRootSignature(effect.Shader->RootSignature.Get());
        }

        void SetConstantsArray(uint rootIndex, uint numConstants, const void* data) const {
            _cmdList->SetGraphicsRoot32BitConstants(rootIndex, numConstants, data, 0);
        }

        void SetConstantBuffer(uint rootIndex, D3D12_GPU_VIRTUAL_ADDRESS cbv) const {
            _cmdList->SetGraphicsRootConstantBufferView(rootIndex, cbv);
        }

        //void SetDynamicConstantBuffer(uint rootIndex, size_t bufferSize, const void* data) {
        //    //_cmdList->SetGraphicsRootConstantBufferView(rootIndex, cbv);

        //    assert(data && IsAligned(data, 16));
        //    auto cb = m_CpuLinearAllocator.Allocate(bufferSize);
        //    //SIMDMemCopy(cb.DataPtr, BufferData, Math::AlignUp(BufferSize, 16) >> 4);
        //    memcpy(cb.DataPtr, data, bufferSize);
        //    _cmdList->SetGraphicsRootConstantBufferView(rootIndex, cb.GpuAddress);
        //}

        void InsertUAVBarrier(const GpuResource& resource) const {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.UAV.pResource = resource.Get();
            _cmdList->ResourceBarrier(1, &barrier);
        }

    private:
        static constexpr bool IsAligned(auto value, size_t alignment) {
            return 0 == ((size_t)value & (alignment - 1));
        }
    };
}
