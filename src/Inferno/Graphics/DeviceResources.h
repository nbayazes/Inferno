//
// DeviceResources.h - A wrapper for the Direct3D 12 device and swapchain
//

#pragma once

#include "DirectX.h"
#include "Heap.h"
#include "Buffers.h"
#include "Settings.h"
#include "CommandContext.h"
#include "IDeviceNotify.h"
#include "PostProcess.h"
#include "ShaderLibrary.h"

namespace Inferno {
    constexpr uint PROBE_RESOLUTION = 128;

    inline void ReportLiveObjects() {
#ifdef _DEBUG
        ComPtr<IDXGIDebug1> dxgiDebug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
            //dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
            dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        }
#endif
    }

    // Controls all the DirectX device resources.
    class DeviceResources {
        bool _typedUAVLoadSupport_R11G11B10_FLOAT = false;
    public:
        static constexpr unsigned int c_AllowTearing = 0x1;
        static constexpr unsigned int c_EnableHDR = 0x2;

        DeviceResources(DXGI_FORMAT backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM,
                        DXGI_FORMAT depthBufferFormat = DXGI_FORMAT_D32_FLOAT,
                        UINT backBufferCount = 2,
                        D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0,
                        unsigned int flags = c_AllowTearing) noexcept(false);
        ~DeviceResources();

        DeviceResources(DeviceResources&&) = default;
        DeviceResources& operator= (DeviceResources&&) = delete;

        DeviceResources(DeviceResources const&) = delete;
        DeviceResources& operator= (DeviceResources const&) = delete;

        float RenderScale = 1.0; // Scaling applied to 3D render targets

        void CreateDeviceResources();
        void CreateWindowSizeDependentResources(bool forceSwapChainRebuild = false);
        void SetWindow(HWND window, int width, int height) noexcept;
        bool WindowSizeChanged(int width, int height);
        void HandleDeviceLost();
        void RegisterDeviceNotify(IDeviceNotify* deviceNotify) noexcept { m_deviceNotify = deviceNotify; }
        //void Prepare();
        void Present();
        void WaitForGpu() const;
        void ReloadResources();

        Vector2 GetOutputSize() const noexcept { return { (float)m_outputSize.right, (float)m_outputSize.bottom }; }
        uint GetWidth() const noexcept { return m_outputSize.right; }
        uint GetHeight() const noexcept { return m_outputSize.bottom; }

        // Direct3D Accessors.
        auto                        Device() const noexcept { return m_d3dDevice.Get(); }
        auto                        GetSwapChain() const noexcept { return m_swapChain.Get(); }
        auto                        GetDXGIFactory() const noexcept { return m_dxgiFactory.Get(); }
        HWND                        GetWindow() const noexcept { return m_window; }
        D3D_FEATURE_LEVEL           GetDeviceFeatureLevel() const noexcept { return m_d3dFeatureLevel; }

        // Gets the active render target
        auto GetBackBuffer() noexcept { return &BackBuffers[m_backBufferIndex]; }
        ID3D12CommandQueue* GetCommandQueue() const noexcept { return CommandQueue->Get(); }
        Ptr<Graphics::CommandQueue> CommandQueue, CopyQueue, BatchUploadQueue, AsyncBatchUploadQueue;

        Graphics::GraphicsContext& GetGraphicsContext() const { return *_graphicsContext[m_backBufferIndex].get(); }

        //ID3D12CommandAllocator* GetCommandAllocator() const noexcept { return m_commandAllocators[m_backBufferIndex].Get(); }
        //auto                        GetCommandList() const noexcept { return m_commandList.Get(); }
        DXGI_FORMAT                 GetBackBufferFormat() const noexcept { return m_backBufferFormat; }
        D3D12_VIEWPORT              GetScreenViewport() const noexcept { return m_screenViewport; }
        D3D12_RECT                  GetScissorRect() const noexcept { return m_scissorRect; }
        UINT                        GetCurrentFrameIndex() const noexcept { return m_backBufferIndex; }
        UINT                        GetBackBufferCount() const noexcept { return m_backBufferCount; }
        DXGI_COLOR_SPACE_TYPE       GetColorSpace() const noexcept { return m_colorSpace; }
        unsigned int                GetDeviceOptions() const noexcept { return m_options; }

        // Both MSAA and normal render targets are necessary when using MSAA.
        // The MSAA buffers are resolved to normal sources before being drawn
        Inferno::ColorBuffer MsaaLinearizedDepthBuffer;
        Inferno::ColorBuffer /*MsaaDistortionBuffer,*/ DistortionBuffer; // Color buffers for distortion effects
        Inferno::RenderTarget BriefingRobot, BriefingRobotMsaa;
        Inferno::DepthBuffer BriefingRobotDepth, BriefingRobotDepthMsaa;

        Inferno::RenderTarget& GetBriefingRobotBuffer() {
            return Settings::Graphics.MsaaSamples > 1 ? BriefingRobotMsaa : BriefingRobot;
        }

        Inferno::DepthBuffer& GetBriefingRobotDepthBuffer() {
            return Settings::Graphics.MsaaSamples > 1 ? BriefingRobotDepthMsaa : BriefingRobotDepth;
        }

        Inferno::RenderTarget SceneColorBuffer, SceneColorBufferMsaa;
        Inferno::DepthBuffer SceneDepthBuffer, SceneDepthBufferMsaa;
        Inferno::ColorBuffer LinearizedDepthBuffer;

        Inferno::RenderTarget BriefingColorBuffer;
        Inferno::RenderTarget BriefingScanlineBuffer;

        DescriptorHandle NullCube; // Null cubemap descriptor

        UploadBuffer<FrameConstants> FrameConstantsBuffer[2] = { { 2, L"Frame constants" }, { 2, L"Frame constants" } };
        UploadBuffer<FrameConstants>& GetFrameConstants() { return FrameConstantsBuffer[GetCurrentFrameIndex()]; }

        UploadBuffer<FrameConstants> TerrainConstantsBuffer[2] = { { 2, L"Terrain constants" }, { 2, L"Terrain constants" } };
        UploadBuffer<FrameConstants>& GetTerrainConstants() { return TerrainConstantsBuffer[GetCurrentFrameIndex()]; }

        UploadBuffer<FrameConstants> BriefingFrameConstantsBuffer[2] = { { 2, L"Briefing constants" }, { 2, L"Briefing constants" } };
        UploadBuffer<FrameConstants>& GetBriefingFrameConstants() { return BriefingFrameConstantsBuffer[GetCurrentFrameIndex()]; }

        PostFx::ScanlineCS Scanline;

        // Gets an intermediate buffer with HDR support
        Inferno::RenderTarget& GetHdrRenderTarget() {
            return Settings::Graphics.MsaaSamples > 1 ? SceneColorBufferMsaa : SceneColorBuffer;
        }

        // There's nothing special about the depth buffer for HDR, but MSAA needs a different one.
        Inferno::DepthBuffer& GetHdrDepthBuffer() {
            return Settings::Graphics.MsaaSamples > 1 ? SceneDepthBufferMsaa : SceneDepthBuffer;
        }

        Inferno::ColorBuffer& GetLinearDepthBuffer() {
            return Settings::Graphics.MsaaSamples > 1 ? MsaaLinearizedDepthBuffer : LinearizedDepthBuffer;
        }

        /*Inferno::ColorBuffer& GetDistortionBuffer() {
            return Settings::Graphics.MsaaSamples > 1 ? MsaaDistortionBuffer : DistortionBuffer;
        }*/

        static constexpr size_t MAX_BACK_BUFFER_COUNT = 2;
        RenderTarget BackBuffers[MAX_BACK_BUFFER_COUNT];

        bool TypedUAVLoadSupport_R11G11B10_FLOAT() const noexcept { return _typedUAVLoadSupport_R11G11B10_FLOAT; }

        D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView() const noexcept {
            return Render::Heaps->RenderTargets[m_backBufferIndex].GetCpuHandle();
        }

        static D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() noexcept {
            return Render::Heaps->DepthStencil[0].GetCpuHandle();
            //return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        }

        static void PrintMemoryUsage();

        // Note that 4x MSAA and 8x MSAA is required for Direct3D Feature Level 11.0 or better
        bool CheckMsaaSupport(uint samples, DXGI_FORMAT backBufferFormat) const;

    private:
        void MoveToNextFrame();
        void GetAdapter(IDXGIAdapter1** ppAdapter) const;
        void UpdateColorSpace();
        void CreateBuffers(UINT width, UINT height);

        UINT m_backBufferIndex;

        Ptr<Graphics::GraphicsContext> _graphicsContext[2];
        // Direct3D objects.
        Microsoft::WRL::ComPtr<ID3D12Device>                m_d3dDevice;
        //Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>   m_commandList;

        // Command queue must be shared between all command lists that write to the swap chain
        //Ptr<Graphics::CommandQueue>          m_commandQueue;
        //Microsoft::WRL::ComPtr<ID3D12CommandAllocator>      m_commandAllocators[MAX_BACK_BUFFER_COUNT];

        // Swap chain objects.
        Microsoft::WRL::ComPtr<IDXGIFactory4>               m_dxgiFactory;
        Microsoft::WRL::ComPtr<IDXGISwapChain3>             m_swapChain;
        //Microsoft::WRL::ComPtr<ID3D12Resource>              m_renderTargets[MAX_BACK_BUFFER_COUNT];
        //Microsoft::WRL::ComPtr<ID3D12Resource>              m_depthStencil;

        // Direct3D rendering objects.
        D3D12_VIEWPORT                                      m_screenViewport;
        D3D12_RECT                                          m_scissorRect;

        // Direct3D properties.
        DXGI_FORMAT                                         m_backBufferFormat;
        DXGI_FORMAT                                         m_depthBufferFormat;
        UINT                                                m_backBufferCount;
        D3D_FEATURE_LEVEL                                   m_d3dMinFeatureLevel;

        // Cached device properties.
        HWND                                                m_window;
        D3D_FEATURE_LEVEL                                   m_d3dFeatureLevel;
        DWORD                                               m_dxgiFactoryFlags;
        RECT                                                m_outputSize;

        // HDR Support
        DXGI_COLOR_SPACE_TYPE                               m_colorSpace;

        // DeviceResources options (see flags above)
        unsigned int                                        m_options;

        // The IDeviceNotify can be held directly as it owns the DeviceResources.
        IDeviceNotify* m_deviceNotify;
    };
}
