#pragma once

#include "Types.h"
#include "DirectX.h"

namespace Inferno::Graphics {

    class GraphicsContext {
        ComPtr<ID3D12GraphicsCommandList> _cmdList;
        ComPtr<ID3D12CommandAllocator> _allocator;

    public:
        GraphicsContext(ID3D12Device* device, wstring_view name) {
            auto type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&_allocator)));
            _allocator->SetName(name.data());

            ThrowIfFailed(device->CreateCommandList(0, type, _allocator.Get(), nullptr, IID_PPV_ARGS(&_cmdList)));
            ThrowIfFailed(_cmdList->Close());

            _cmdList->SetName(name.data());
        }


        ~GraphicsContext() = default;
        GraphicsContext(const GraphicsContext&) = delete;
        GraphicsContext(GraphicsContext&&) = default;
        GraphicsContext& operator=(const GraphicsContext&) = delete;
        GraphicsContext& operator=(GraphicsContext&&) = default;


        void Reset() {
            //auto& allocators = m_commandAllocators[m_backBufferIndex];
            ThrowIfFailed(_allocator->Reset());
            ThrowIfFailed(_cmdList->Reset(_allocator.Get(), nullptr));
        }

        //void Prepare(ID3D12CommandAllocator* allocator) {
        //    // Reset command list and allocator.
        //    //auto& allocators = m_commandAllocators[m_backBufferIndex];
        //    //ThrowIfFailed(allocators->Reset());
        //    ThrowIfFailed(_cmdList->Reset(allocator, nullptr));
        //}

        // Present the contents of the swap chain to the screen.
        //void Present() {
        //    //BackBuffers[m_backBufferIndex].Transition(m_commandList.Get(), D3D12_RESOURCE_STATE_PRESENT);

        //    // Send the command list off to the GPU for processing.
        //    ThrowIfFailed(_cmdList->Close());
        //    m_commandQueue->ExecuteCommandLists(1, CommandListCast(m_commandList.GetAddressOf()));
        //}

        ID3D12GraphicsCommandList* CommandList() { return _cmdList.Get(); }

        void SetRenderTargets(span<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs, D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
            _cmdList->OMSetRenderTargets(rtvs.size(), rtvs.data(), false, &dsv);
        }

        void SetRenderTargets(span<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs) {
            _cmdList->OMSetRenderTargets(rtvs.size(), rtvs.data(), false, nullptr);
        }

        void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv) { SetRenderTargets({ &rtv, 1 }); }
        void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv) { SetRenderTargets({ &rtv, 1 }, dsv); }

        void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) {
            _cmdList->IASetPrimitiveTopology(topology);
        }

        void ClearColor(RenderTarget& target, D3D12_RECT* rect = nullptr) {
            //FlushResourceBarriers();
            //_cmdList->ClearRenderTargetView(target.GetRTV(), target.ClearColor, (rect == nullptr) ? 0 : 1, rect);
            target.Clear(_cmdList.Get());
        }

        void ClearDepth(DepthBuffer& target, D3D12_RECT* rect = nullptr) {
            target.Clear(_cmdList.Get());
            //FlushResourceBarriers();
            //target.Transition(_cmdList.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
            //_cmdList->ClearDepthStencilView(target->get _descriptor.GetCpuHandle(), D3D12_CLEAR_FLAG_DEPTH, StencilDepth, 0, 0, nullptr);
        }

        void BeginEvent(wstring_view name) {
            PIXBeginEvent(_cmdList.Get(), PIX_COLOR_DEFAULT, name.data());
        }

        void EndEvent() {
            PIXEndEvent(_cmdList.Get());
        }
    };

}