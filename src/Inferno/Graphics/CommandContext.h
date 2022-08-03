#pragma once

#include "Types.h"
#include "DirectX.h"
#include "ShaderLibrary.h"

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
            ThrowIfFailed(_allocator->Reset());
            ThrowIfFailed(_cmdList->Reset(_allocator.Get(), nullptr));
        }

        ID3D12GraphicsCommandList* CommandList() { return _cmdList.Get(); }

        // Sets multiple render targets with a depth buffer. Used with shaders that write to multiple buffers.
        void SetRenderTargets(span<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs, D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
            _cmdList->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), false, &dsv);
        }

        // Sets multiple render targets. Used with shaders that write to multiple buffers.
        void SetRenderTargets(span<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs) {
            _cmdList->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), false, nullptr);
        }

        void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv) { SetRenderTargets({ &rtv, 1 }); }
        void SetRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, D3D12_CPU_DESCRIPTOR_HANDLE dsv) { SetRenderTargets({ &rtv, 1 }, dsv); }

        void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) {
            _cmdList->IASetPrimitiveTopology(topology);
        }

        void ClearColor(RenderTarget& target, D3D12_RECT* rect = nullptr) {
            //FlushResourceBarriers();
            target.Transition(_cmdList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
            _cmdList->ClearRenderTargetView(target.GetRTV(), target.ClearColor, (rect == nullptr) ? 0 : 1, rect);
        }

        void ClearDepth(DepthBuffer& target, D3D12_RECT* rect = nullptr) {
            //FlushResourceBarriers();
            target.Transition(_cmdList.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
            _cmdList->ClearDepthStencilView(target.GetDSV(), D3D12_CLEAR_FLAG_DEPTH, target.StencilDepth, 0, (rect == nullptr) ? 0 : 1, rect);
        }

        void SetViewportAndScissor(UINT width, UINT height) {
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

        void BeginEvent(wstring_view name) {
            PIXBeginEvent(_cmdList.Get(), PIX_COLOR_DEFAULT, name.data());
        }

        void EndEvent() {
            PIXEndEvent(_cmdList.Get());
        }

        template<class T>
        void ApplyEffect(const Effect<T>& effect) {
            _cmdList->SetPipelineState(effect.PipelineState.Get());
            _cmdList->SetGraphicsRootSignature(effect.Shader->RootSignature.Get());
        }

        void SetConstantsArray(uint rootIndex, uint numConstants, const void *data) {
            _cmdList->SetGraphicsRoot32BitConstants(rootIndex, numConstants, data, 0);
        }
    };

}