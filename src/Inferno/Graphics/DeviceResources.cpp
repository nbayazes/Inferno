//
// DeviceResources.cpp - A wrapper for the Direct3D 12 device and swapchain
//

#include "pch.h"
#include "DeviceResources.h"
#include "Render.h"
#include "Types.h"
#include "ScopedTimer.h"
#include <dxgi1_6.h>

namespace Inferno {
    using namespace DirectX;

#pragma warning(disable : 4061)

    // intermediate format for rendering. need to switch to 32 bit for bloom effects.
    constexpr DXGI_FORMAT IntermediateFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    //constexpr DXGI_FORMAT IntermediateFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    namespace {
        constexpr DXGI_FORMAT StripSRGB(DXGI_FORMAT fmt) noexcept {
            switch (fmt) {
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
                case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
                default: return fmt;
            }
        }
    }

    // Constructor for DeviceResources.
    DeviceResources::DeviceResources(
        DXGI_FORMAT backBufferFormat,
        DXGI_FORMAT depthBufferFormat,
        UINT backBufferCount,
        D3D_FEATURE_LEVEL minFeatureLevel,
        unsigned int flags) noexcept(false) :
        m_backBufferIndex(0),
        m_screenViewport{},
        m_scissorRect{},
        m_backBufferFormat(backBufferFormat),
        m_depthBufferFormat(depthBufferFormat),
        m_backBufferCount(backBufferCount),
        m_d3dMinFeatureLevel(minFeatureLevel),
        m_window(nullptr),
        m_d3dFeatureLevel(D3D_FEATURE_LEVEL_11_0),
        m_dxgiFactoryFlags(0),
        m_outputSize{ 0, 0, 1, 1 },
        m_colorSpace(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709),
        m_options(flags),
        m_deviceNotify(nullptr) {
        if (backBufferCount < 2 || backBufferCount > MAX_BACK_BUFFER_COUNT)
            throw std::out_of_range("invalid backBufferCount");

        if (minFeatureLevel < D3D_FEATURE_LEVEL_11_0)
            throw std::out_of_range("minFeatureLevel too low");
    }

    // Destructor for DeviceResources.
    DeviceResources::~DeviceResources() {
        // Ensure that the GPU is no longer referencing resources that are about to be destroyed.
        WaitForGpu();
    }

    // Configures the Direct3D device, and stores handles to it and the device context.
    void DeviceResources::CreateDeviceResources() {

#ifdef _DEBUG
        // Enable the debug layer (requires the Graphics Tools "optional feature").
        //
        // NOTE: Enabling the debug layer after device creation will invalidate the active device.
        {
            //ComPtr<IDXGraphicsAnalysis> graphics_analysis;
            //const auto result = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&graphics_analysis));
            //if (FAILED(result)) {
                // Not running in PIX, enable the debug layer
                ComPtr<ID3D12Debug> debugController;
                if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
                    OutputDebugStringA("Direct3D Debug Layer Enabled\n");
                    debugController->EnableDebugLayer();
                }
                else {
                    OutputDebugStringA("WARNING: Direct3D Debug Device is not available\n");
                }
            //}

            ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
            if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf())))) {
                m_dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;

                dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
                dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);

                /* IDXGISwapChain::GetContainingOutput: The swapchain's adapter does not control the output on which the swapchain's window resides. */
                DXGI_INFO_QUEUE_MESSAGE_ID hide[] = { 80 };
                DXGI_INFO_QUEUE_FILTER filter = {};
                filter.DenyList.NumIDs = _countof(hide);
                filter.DenyList.pIDList = hide;
                dxgiInfoQueue->AddStorageFilterEntries(DXGI_DEBUG_DXGI, &filter);
            }
        }
#endif

        ThrowIfFailed(CreateDXGIFactory2(m_dxgiFactoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));

        // Determines whether tearing support is available for fullscreen borderless windows.
        if (m_options & c_AllowTearing) {
            BOOL allowTearing = false;

            ComPtr<IDXGIFactory5> factory5;
            HRESULT hr = m_dxgiFactory.As(&factory5);
            if (SUCCEEDED(hr))
                hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));

            if (FAILED(hr) || !allowTearing) {
                m_options &= ~c_AllowTearing;
                OutputDebugStringA("WARNING: Variable refresh rate displays not supported");
            }
        }

        ComPtr<IDXGIAdapter1> adapter;
        GetAdapter(adapter.GetAddressOf());

        // Create the DX12 API device object.
        ThrowIfFailed(D3D12CreateDevice(
            adapter.Get(),
            m_d3dMinFeatureLevel,
            IID_PPV_ARGS(m_d3dDevice.ReleaseAndGetAddressOf())
        ));

        m_d3dDevice->SetName(L"D3D Device");

#ifndef NDEBUG
        // Configure debug device (if active).
        ComPtr<ID3D12InfoQueue> d3dInfoQueue;
        if (SUCCEEDED(m_d3dDevice.As(&d3dInfoQueue))) {
#ifdef _DEBUG
            d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
#endif
            D3D12_MESSAGE_ID hide[] = {
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_EXECUTECOMMANDLISTS_WRONGSWAPCHAINBUFFERREFERENCE
            };
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = _countof(hide);
            filter.DenyList.pIDList = hide;
            d3dInfoQueue->AddStorageFilterEntries(&filter);
        }
#endif

        // Determine maximum supported feature level for this device
        static const D3D_FEATURE_LEVEL s_featureLevels[] = {
            D3D_FEATURE_LEVEL_12_2, // Requires agility SDK on Windows 10
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        D3D12_FEATURE_DATA_FEATURE_LEVELS featLevels = {
            _countof(s_featureLevels), s_featureLevels, D3D_FEATURE_LEVEL_11_0
        };

        HRESULT hr = m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featLevels, sizeof(featLevels));
        m_d3dFeatureLevel = SUCCEEDED(hr) ? featLevels.MaxSupportedFeatureLevel : m_d3dMinFeatureLevel;

        {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT Support = {
                DXGI_FORMAT_R11G11B10_FLOAT,
                D3D12_FORMAT_SUPPORT1_NONE,
                D3D12_FORMAT_SUPPORT2_NONE
            };

            if (SUCCEEDED(m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))) &&
                (Support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0) {
                _typedUAVLoadSupport_R11G11B10_FLOAT = true;
                SPDLOG_INFO("GPU supports R11G11B10 UAV Loading");
            }
        }

        Render::Device = m_d3dDevice.Get();

        // Create the command queues
        CommandQueue = MakePtr<Graphics::CommandQueue>(m_d3dDevice.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, L"DeviceResources Command Queue");
        BatchUploadQueue = MakePtr<Graphics::CommandQueue>(m_d3dDevice.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, L"DeviceResources Batch Queue");
        AsyncBatchUploadQueue = MakePtr<Graphics::CommandQueue>(m_d3dDevice.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT, L"DeviceResources Batch Queue");
        CopyQueue = MakePtr<Graphics::CommandQueue>(m_d3dDevice.Get(), D3D12_COMMAND_LIST_TYPE_COPY, L"DeviceResources Copy Queue");

        // Create a command allocator for each back buffer that will be rendered to.
        for (UINT n = 0; n < m_backBufferCount; n++) {
            _graphicsContext[n] = MakePtr<Graphics::GraphicsContext>(m_d3dDevice.Get(), CommandQueue.get(), fmt::format(L"Render target {}", n));
            //ThrowIfFailed(m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocators[n].ReleaseAndGetAddressOf())));
            //m_commandAllocators[n]->SetName();
        }

        // Create a command list for recording graphics commands.
        //ThrowIfFailed(m_d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(m_commandList.ReleaseAndGetAddressOf())));
        //ThrowIfFailed(m_commandList->Close());

        //m_commandList->SetName(L"DeviceResources");

        // Create a fence for tracking GPU execution progress.
        //ThrowIfFailed(m_d3dDevice->CreateFence(m_fenceValues[m_backBufferIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf())));
        //m_fenceValues[m_backBufferIndex]++;

        //ThrowIfFailed(m_fence->SetName(L"Fence"));

        //m_fenceEvent.Attach(CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE));
        //if (!m_fenceEvent.IsValid())
        //    throw std::exception("CreateEvent");

        //CheckMsaaSupport(2, IntermediateFormat);
        //CheckMsaaSupport(4, IntermediateFormat);
        //CheckMsaaSupport(8, IntermediateFormat);
    }

    // These resources need to be recreated every time the window size is changed.
    void DeviceResources::CreateWindowSizeDependentResources() {
        if (!m_window)
            throw std::exception("Call SetWindow with a valid Win32 window handle");

        WaitForGpu(); // Wait until all previous GPU work is complete.

        // Release resources that are tied to the swap chain and update fence values.
        for (UINT n = 0; n < m_backBufferCount; n++) {
            BackBuffers[n].Release();
            //m_fenceValues[n] = m_fenceValues[m_backBufferIndex];
        }

        // Determine the render target size in pixels.
        const auto backBufferWidth = std::max(uint(m_outputSize.right - m_outputSize.left), 1u);
        const auto backBufferHeight = std::max(uint(m_outputSize.bottom - m_outputSize.top), 1u);
        const DXGI_FORMAT backBufferFormat = StripSRGB(m_backBufferFormat);

        CreateBuffers(backBufferWidth, backBufferHeight);

        // If the swap chain already exists, resize it, otherwise create one.
        if (m_swapChain) {
            // If the swap chain already exists, resize it.
            HRESULT hr = m_swapChain->ResizeBuffers(
                m_backBufferCount,
                backBufferWidth,
                backBufferHeight,
                backBufferFormat,
                (m_options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u
            );

            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
#ifdef _DEBUG
                char buff[64] = {};
                sprintf_s(buff, "Device Lost on ResizeBuffers: Reason code 0x%08X\n",
                          static_cast<unsigned int>((hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3dDevice->GetDeviceRemovedReason() : hr));
                OutputDebugStringA(buff);
#endif
                // If the device was removed for any reason, a new device and swap chain will need to be created.
                HandleDeviceLost();

                // Everything is set up now. Do not continue execution of this method. HandleDeviceLost will reenter this method
                // and correctly set up the new device.
                return;
            }
            else {
                ThrowIfFailed(hr);
            }
        }
        else {
            // Create a descriptor for the swap chain.
            DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
            swapChainDesc.Width = backBufferWidth;
            swapChainDesc.Height = backBufferHeight;
            swapChainDesc.Format = backBufferFormat;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.BufferCount = m_backBufferCount;
            swapChainDesc.SampleDesc.Count = 1;
            swapChainDesc.SampleDesc.Quality = 0;
            swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            swapChainDesc.Flags = (m_options & c_AllowTearing) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

            DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
            fsSwapChainDesc.Windowed = TRUE;

            // Create a swap chain for the window.
            ComPtr<IDXGISwapChain1> swapChain;
            ThrowIfFailed(m_dxgiFactory->CreateSwapChainForHwnd(
                CommandQueue->Get(),
                m_window,
                &swapChainDesc,
                &fsSwapChainDesc,
                nullptr,
                swapChain.GetAddressOf()
            ));

            ThrowIfFailed(swapChain.As(&m_swapChain));

            // This class does not support exclusive full-screen mode and prevents DXGI from responding to the ALT+ENTER shortcut
            ThrowIfFailed(m_dxgiFactory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER));
        }

        // Handle color space settings for HDR
        UpdateColorSpace();

        // Obtain the back buffers for this window which will be the final render targets
        // and create render target views for each of them.
        for (UINT n = 0; n < m_backBufferCount; n++) {
            auto name = fmt::format(L"Render target {}", n);
            BackBuffers[n].Create(name, m_swapChain.Get(), n, m_backBufferFormat);
        }

        if (m_depthBufferFormat != DXGI_FORMAT_UNKNOWN) {
            SceneDepthBuffer.Create(L"Depth stencil buffer", backBufferWidth, backBufferHeight, m_depthBufferFormat);
        }

        // Reset the index to the current back buffer.
        m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

        // Set the 3D rendering viewport and scissor rectangle to target the entire window.
        m_screenViewport.TopLeftX = m_screenViewport.TopLeftY = 0.f;
        m_screenViewport.Width = (float)backBufferWidth;
        m_screenViewport.Height = (float)backBufferHeight;
        m_screenViewport.MinDepth = D3D12_MIN_DEPTH;
        m_screenViewport.MaxDepth = D3D12_MAX_DEPTH;

        m_scissorRect.left = m_scissorRect.top = 0;
        m_scissorRect.right = (LONG)backBufferWidth;
        m_scissorRect.bottom = (LONG)backBufferHeight;
    }

    // This method is called when the Win32 window is created (or re-created).
    void DeviceResources::SetWindow(HWND window, int width, int height) noexcept {
        m_window = window;

        m_outputSize.left = m_outputSize.top = 0;
        m_outputSize.right = width;
        m_outputSize.bottom = height;
    }

    // This method is called when the Win32 window changes size.
    bool DeviceResources::WindowSizeChanged(int width, int height) {
        RECT newRc{};
        newRc.left = newRc.top = 0;
        newRc.right = width;
        newRc.bottom = height;
        if (newRc.left == m_outputSize.left
            && newRc.top == m_outputSize.top
            && newRc.right == m_outputSize.right
            && newRc.bottom == m_outputSize.bottom) {
            // Handle color space settings for HDR
            UpdateColorSpace();

            return false;
        }

        m_outputSize = newRc;
        CreateWindowSizeDependentResources();
        return true;
    }

    // Recreate all device resources and set them back to the current state.
    void DeviceResources::HandleDeviceLost() {
        if (m_deviceNotify) {
            m_deviceNotify->OnDeviceLost();
        }

        for (UINT n = 0; n < m_backBufferCount; n++) {
            _graphicsContext[n].release();
            BackBuffers[n].Release();
        }

        CommandQueue.reset();
        BatchUploadQueue.reset();
        AsyncBatchUploadQueue.reset();
        CopyQueue.reset();
        m_swapChain.Reset();
        m_d3dDevice.Reset();
        m_dxgiFactory.Reset();

        ReportLiveObjects();
        CreateDeviceResources();
        CreateWindowSizeDependentResources();

        if (m_deviceNotify) {
            m_deviceNotify->OnDeviceRestored();
        }
    }

    // Present the contents of the swap chain to the screen.
    void DeviceResources::Present() {
        auto& context = _graphicsContext[m_backBufferIndex];
        auto cmdList = context->GetCommandList();
        BackBuffers[m_backBufferIndex].Transition(cmdList, D3D12_RESOURCE_STATE_PRESENT);
        context->Execute();

        HRESULT hr{};
        if (m_options & c_AllowTearing) {
            // Recommended to always use tearing if supported when using a sync interval of 0.
            // Note this will fail if in true 'fullscreen' mode.
            hr = m_swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
        }
        else {
            // The first argument instructs DXGI to block until VSync, putting the application
            // to sleep until the next VSync. This ensures we don't waste any cycles rendering
            // frames that will never be displayed to the screen.
            hr = m_swapChain->Present(1, 0);
        }

        // If the device was reset we must completely reinitialize the renderer.
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
#ifdef _DEBUG
            char buff[64] = {};
            sprintf_s(buff, "Device Lost on Present: Reason code 0x%08X\n",
                      static_cast<unsigned int>((hr == DXGI_ERROR_DEVICE_REMOVED) ? m_d3dDevice->GetDeviceRemovedReason() : hr));
            OutputDebugStringA(buff);
#endif
            HandleDeviceLost();
        }
        else {
            ThrowIfFailed(hr);

            MoveToNextFrame();

            if (!m_dxgiFactory->IsCurrent()) {
                // Output information is cached on the DXGI Factory. If it is stale we need to create a new factory.
                ThrowIfFailed(CreateDXGIFactory2(m_dxgiFactoryFlags, IID_PPV_ARGS(m_dxgiFactory.ReleaseAndGetAddressOf())));
            }
        }
    }

    // Wait for pending GPU work to complete.
    void DeviceResources::WaitForGpu() const {
        _graphicsContext[m_backBufferIndex]->WaitForIdle();
    }

    void DeviceResources::ReloadResources() {
        int64 time = {};
        {
            ScopedTimer timer(&time);

            WaitForGpu();

            auto width = m_outputSize.right;
            auto height = m_outputSize.bottom;

            Render::Effects->Compile(m_d3dDevice.Get(), Settings::Graphics.MsaaSamples);
            Scanline.Load(L"shaders/ScanlineCS.hlsl");
            Render::LightGrid->Load(L"shaders/FillLightGridCS.hlsl");
            Render::Bloom->ReloadShaders();

            CreateBuffers(width, height);
            PrintMemoryUsage();
        }
        SPDLOG_INFO("GPU Resource reload time: {:.2f} ms", time / 1000.0f);
    }

    // Prepare to render the next frame.
    void DeviceResources::MoveToNextFrame() {
        m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
        auto& nextFrame = GetGraphicsContext();
        nextFrame.WaitForIdle(); // wait on the next frame to finish rendering before recording new commands
    }

    // This method acquires the first available hardware adapter that supports Direct3D 12.
    // If no such adapter can be found, try WARP. Otherwise throw an exception.
    void DeviceResources::GetAdapter(IDXGIAdapter1** ppAdapter) const {
        *ppAdapter = nullptr;

        ComPtr<IDXGIAdapter1> adapter;

#if defined(__dxgi1_6_h__) && defined(NTDDI_WIN10_RS4)
        ComPtr<IDXGIFactory6> factory6;
        HRESULT hr = m_dxgiFactory.As(&factory6);
        if (SUCCEEDED(hr)) {
            for (UINT adapterIndex = 0;
                 SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                     adapterIndex,
                     DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                     IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())));
                 adapterIndex++) {
                DXGI_ADAPTER_DESC1 desc;
                ThrowIfFailed(adapter->GetDesc1(&desc));

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                    // Don't select the Basic Render Driver adapter.
                    continue;
                }

                // Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), m_d3dMinFeatureLevel, _uuidof(ID3D12Device), nullptr))) {
#ifdef _DEBUG
                    wchar_t buff[256] = {};
                    swprintf_s(buff, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);
                    OutputDebugStringW(buff);
#endif
                    break;
                }
            }
        }
#endif
        if (!adapter) {
            for (UINT adapterIndex = 0;
                 SUCCEEDED(m_dxgiFactory->EnumAdapters1(
                     adapterIndex,
                     adapter.ReleaseAndGetAddressOf()));
                 ++adapterIndex) {
                DXGI_ADAPTER_DESC1 desc;
                ThrowIfFailed(adapter->GetDesc1(&desc));

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                    // Don't select the Basic Render Driver adapter.
                    continue;
                }

                // Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), m_d3dMinFeatureLevel, _uuidof(ID3D12Device), nullptr))) {
#ifdef _DEBUG
                    wchar_t buff[256] = {};
                    swprintf_s(buff, L"Direct3D Adapter (%u): VID:%04X, PID:%04X - %ls\n", adapterIndex, desc.VendorId, desc.DeviceId, desc.Description);
                    OutputDebugStringW(buff);
#endif
                    break;
                }
            }
        }

#if !defined(NDEBUG)
        if (!adapter) {
            // Try WARP12 instead
            if (FAILED(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf())))) {
                throw std::exception("WARP12 not available. Enable the 'Graphics Tools' optional feature");
            }

            OutputDebugStringA("Direct3D Adapter - WARP12\n");
        }
#endif

        if (!adapter)
            throw std::exception("No Direct3D 12 device found");

        *ppAdapter = adapter.Detach();
    }

    // Sets the color space for the swap chain in order to handle HDR output.
    void DeviceResources::UpdateColorSpace() {
        DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

        bool isDisplayHDR10 = false;

#if defined(NTDDI_WIN10_RS2)
        if (m_swapChain) {
            ComPtr<IDXGIOutput> output;
            if (SUCCEEDED(m_swapChain->GetContainingOutput(output.GetAddressOf()))) {
                ComPtr<IDXGIOutput6> output6;
                if (SUCCEEDED(output.As(&output6))) {
                    DXGI_OUTPUT_DESC1 desc;
                    ThrowIfFailed(output6->GetDesc1(&desc));

                    if (desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                        // Display output is HDR10.
                        isDisplayHDR10 = true;
                    }
                }
            }
        }
#endif

        if ((m_options & c_EnableHDR) && isDisplayHDR10) {
            switch (m_backBufferFormat) {
                case DXGI_FORMAT_R10G10B10A2_UNORM:
                    // The application creates the HDR10 signal.
                    colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                    break;

                case DXGI_FORMAT_R16G16B16A16_FLOAT:
                    // The system creates the HDR10 signal; application uses linear values.
                    colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
                    break;

                default:
                    break;
            }
        }

        m_colorSpace = colorSpace;

        UINT colorSpaceSupport = 0;
        if (SUCCEEDED(m_swapChain->CheckColorSpaceSupport(colorSpace, &colorSpaceSupport))
            && (colorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
            ThrowIfFailed(m_swapChain->SetColorSpace1(colorSpace));
        }
    }

    void DeviceResources::CreateBuffers(UINT width, UINT height) {
        // Order of buffer creation matters
        Color clearColor(0.1f, 0.1f, 0.1f);
        clearColor.x = std::pow(clearColor.x, 2.2f);
        clearColor.y = std::pow(clearColor.y, 2.2f);
        clearColor.z = std::pow(clearColor.z, 2.2f);

        LinearizedDepthBuffer.Create(L"Linear depth buffer", width, height, DepthShader::OutputFormat);
        LinearizedDepthBuffer.AddShaderResourceView();
        LinearizedDepthBuffer.AddUnorderedAccessView();
        LinearizedDepthBuffer.AddRenderTargetView();
        SceneColorBuffer.Create(L"Scene color buffer", width, height, IntermediateFormat, clearColor, 1);
        SceneColorBuffer.AddUnorderedAccessView();
        DistortionBuffer.Create(L"Scene distortion buffer", width, height, IntermediateFormat, 1);
        DistortionBuffer.AddShaderResourceView();
        SceneDepthBuffer.Create(L"Scene depth buffer", width, height, m_depthBufferFormat, 1);
        BriefingColorBuffer.Create(L"Briefing color buffer", 640, 480, DXGI_FORMAT_R8G8B8A8_UNORM, { 0, 0, 0, 0 });
        BriefingScanlineBuffer.Create(L"Briefing scanline buffer", 640, 480, DXGI_FORMAT_R8G8B8A8_UNORM, { 0, 0, 0, 0 });
        BriefingScanlineBuffer.AddUnorderedAccessView();
        //FrameConstantsBuffer.CreateGenericBuffer(L"Frame constants");

        if (Settings::Graphics.MsaaSamples > 1) {
            MsaaColorBuffer.Create(L"MSAA Color Buffer", width, height, IntermediateFormat, clearColor, Settings::Graphics.MsaaSamples);
            MsaaDepthBuffer.Create(L"MSAA Depth Buffer", width, height, m_depthBufferFormat, Settings::Graphics.MsaaSamples);
            MsaaLinearizedDepthBuffer.Create(L"MSAA Linear depth buffer", width, height, DepthShader::OutputFormat, Settings::Graphics.MsaaSamples);
            MsaaLinearizedDepthBuffer.AddRenderTargetView();
            MsaaLinearizedDepthBuffer.AddShaderResourceView();
            //MsaaDistortionBuffer.Create(L"MSAA distortion buffer", width, height, IntermediateFormat, Settings::Graphics.MsaaSamples);
            //MsaaDistortionBuffer.AddShaderResourceView();
        }
        else {
            MsaaColorBuffer.Release();
            MsaaDepthBuffer.Release();
            MsaaLinearizedDepthBuffer.Release();
            //MsaaDistortionBuffer.Release();
        }
    }

    void DeviceResources::PrintMemoryUsage() {
        try {
            ComPtr<IDXGIFactory4> dxgiFactory;
            ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)));

            ComPtr<IDXGIAdapter3> dxgiAdapter;
            ComPtr<IDXGIAdapter1> tmpDxgiAdapter;
            UINT adapterIndex = 0;
            while (dxgiFactory->EnumAdapters1(adapterIndex, &tmpDxgiAdapter) != DXGI_ERROR_NOT_FOUND) {
                DXGI_ADAPTER_DESC1 desc;
                ThrowIfFailed(tmpDxgiAdapter->GetDesc1(&desc));
                if (!dxgiAdapter && desc.Flags == 0) // Flags == 0 filters to hardware GPUs (not software or remote)
                    ThrowIfFailed(tmpDxgiAdapter->QueryInterface(IID_PPV_ARGS(&dxgiAdapter)));
                ++adapterIndex;
            }

            DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
            ThrowIfFailed(dxgiAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info));

            //SPDLOG_INFO("Graphics memory:\r\nAvailable: {} MB\r\nBudget: {} MB\r\nCurrent Reservation: {} MB\r\nCurrent Usage: {} MB",
            //            info.AvailableForReservation / 1024 / 1024, // Reservation hints OS at the minimum memory required for the app to run
            //            info.Budget / 1024 / 1024, // The available memory for usage
            //            info.CurrentReservation / 1024 / 1024,
            //            info.CurrentUsage / 1024 / 1024);

            SPDLOG_INFO("Graphics memory usage: {} / {} MB",
                        info.CurrentUsage / 1024 / 1024,
                        info.Budget / 1024 / 1024);
        }
        catch (...) {
            SPDLOG_ERROR("Error querying GPU memory usage");
        }
    }

    // Note that 4x MSAA and 8x MSAA is required for Direct3D Feature Level 11.0 or better

    bool Inferno::DeviceResources::CheckMsaaSupport(uint samples, DXGI_FORMAT backBufferFormat) const {
        SPDLOG_INFO("Checking MSAA Support. Samples {}", samples);
        for (auto sampleCount = samples; sampleCount > 0; sampleCount--) {
            if (sampleCount == 1) {
                SPDLOG_INFO("MSAA Is not supported");
                return false;
            }

            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = { backBufferFormat, sampleCount };
            if (FAILED(m_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))))
                continue;

            if (levels.NumQualityLevels > 0) {
                SPDLOG_INFO("Samples: {} Quality: {}", levels.SampleCount, levels.NumQualityLevels);
                return true;
            }
        }

        return false;
    }
}
