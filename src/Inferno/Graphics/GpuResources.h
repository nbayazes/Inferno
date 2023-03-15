#pragma once

#include "DirectX.h"
#include "Types.h"

using Microsoft::WRL::ComPtr;

namespace Inferno {
    class GpuResource {
    protected:
        ComPtr<ID3D12Resource> _resource;
        D3D12_RESOURCE_STATES _state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_DESC _desc = {};
        D3D12_HEAP_TYPE _heapType = {};
        wstring _name;

        DescriptorHandle _srv, _rtv, _uav;
        D3D12_RENDER_TARGET_VIEW_DESC _rtvDesc = {};
        D3D12_SHADER_RESOURCE_VIEW_DESC _srvDesc = {};
        D3D12_UNORDERED_ACCESS_VIEW_DESC _uavDesc = {};

    public:
        GpuResource() = default;

        virtual ~GpuResource() = default;
        GpuResource(const GpuResource&) = delete;
        GpuResource(GpuResource&&) = default;
        GpuResource& operator=(const GpuResource&) = delete;
        GpuResource& operator=(GpuResource&&) = default;

        ID3D12Resource* Get() const { return _resource.Get(); }
        ID3D12Resource* operator->() { return _resource.Get(); }
        const ID3D12Resource* operator->() const { return _resource.Get(); }
        operator bool() const { return _resource.Get() != nullptr; }
        void Release() { _resource.Reset(); }
        D3D12_RESOURCE_DESC& Description() { return _desc; }

        const auto GetSRV() const { return _srv.GetGpuHandle(); }
        const auto GetUAV() const { return _uav.GetGpuHandle(); }
        const auto GetRTV() const { return _rtv.GetCpuHandle(); }

        // Returns the original state
        D3D12_RESOURCE_STATES Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES state) {
            if (_state == state) return _state;
            DirectX::TransitionResource(cmdList, _resource.Get(), _state, state);

            if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
                barrier.UAV.pResource = _resource.Get();
                cmdList->ResourceBarrier(1, &barrier);
            }
            auto originalState = _state;
            _state = state;
            return originalState;
        }

        void CreateOnUploadHeap(wstring name, const D3D12_CLEAR_VALUE* clearValue = nullptr) {
            Create(D3D12_HEAP_TYPE_UPLOAD, name, clearValue);
        }

        void CreateOnDefaultHeap(wstring name, const D3D12_CLEAR_VALUE* clearValue = nullptr) {
            Create(D3D12_HEAP_TYPE_DEFAULT, name, clearValue);
        }

        // Creates a resource at a specific location in a heap
        CD3DX12_RESOURCE_BARRIER CreatePlacedResource(ID3D12Device* device,
                                                      ID3D12Heap* heap,
                                                      size_t offset,
                                                      wstring name) {
            ThrowIfFailed(device->CreatePlacedResource(
                heap,
                offset,
                &_desc,
                D3D12_RESOURCE_STATE_COMMON,
                nullptr,
                IID_PPV_ARGS(&_resource)));

            _resource->SetName(name.c_str());
            return CD3DX12_RESOURCE_BARRIER::Aliasing(nullptr, _resource.Get());
        }

        // If desc is null then default initialization is used. Not supported for all resources.
        void CreateShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE dest, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc = nullptr) const {
            Render::Device->CreateShaderResourceView(Get(), desc, dest);
        }

        //// If desc is null then default initialization is used. Not supported for all resources.
        //void CreateUnorderedAccessView(D3D12_CPU_DESCRIPTOR_HANDLE dest, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc = nullptr) const {
        //    Render::Device->CreateUnorderedAccessView(Get(), nullptr, desc, dest);
        //}

        void AddShaderResourceView() {
            assert(Get()); // Call CreateOnUploadHeap or CreateOnDefaultHeap first
            if (!_srv) _srv = Render::Heaps->Reserved.Allocate();
            Render::Device->CreateShaderResourceView(Get(), &_srvDesc, _srv.GetCpuHandle());
        }

        void AddUnorderedAccessView(bool useDefaultDesc = true) {
            assert(Get()); // Call CreateOnUploadHeap or CreateOnDefaultHeap first
            if (!_uav) _uav = Render::Heaps->Reserved.Allocate();
            auto desc = useDefaultDesc ? nullptr : &_uavDesc;
            Render::Device->CreateUnorderedAccessView(Get(), nullptr, desc, _uav.GetCpuHandle());
        }

        void AddRenderTargetView() {
            assert(Get()); // Call CreateOnUploadHeap or CreateOnDefaultHeap first
            if (!_rtv) _rtv = Render::Heaps->RenderTargets.Allocate();
            Render::Device->CreateRenderTargetView(Get(), &_rtvDesc, _rtv.GetCpuHandle());
        }

    private:
        void Create(D3D12_HEAP_TYPE heapType, wstring name, const D3D12_CLEAR_VALUE* clearValue) {
            _heapType = heapType;
            CD3DX12_HEAP_PROPERTIES props(_heapType);
            ThrowIfFailed(Render::Device->CreateCommittedResource(
                &props,
                D3D12_HEAP_FLAG_NONE,
                &_desc,
                _state,
                clearValue,
                IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())));

            _resource->SetName(name.c_str());
            _name = name;
        }
    };

    // General purpose buffer
    class GpuBuffer : public GpuResource {
    public:
        void CreateGenericBuffer(wstring_view name, uint32 elementSize, uint32 elementCount) {
            _desc = CD3DX12_RESOURCE_DESC::Buffer(elementSize * elementCount);
            _state = D3D12_RESOURCE_STATE_GENERIC_READ;

            _srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            _srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Buffer.NumElements = elementCount;
            _srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(Render::Device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &_desc,
                _state,
                nullptr,
                IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())
            ));

            if (!_srv) _srv = Render::Heaps->Reserved.Allocate();
            Render::Device->CreateShaderResourceView(Get(), &_srvDesc, _srv.GetCpuHandle());
            _resource->SetName(name.data());
        }
    };

    class ByteAddressBuffer final : public GpuBuffer {
    public:
        void Create(wstring_view name, uint32 elementSize, uint32 elementCount) {
            _desc = CD3DX12_RESOURCE_DESC::Buffer(elementSize * elementCount, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            _srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            _srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Buffer.NumElements = elementCount / 4;
            _srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

            //m_SRV = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(Render::Device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &_desc,
                _state,
                nullptr,
                IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())
            ));

            //if (!_srv) _srv = Render::Heaps->Reserved.Allocate();

            _uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            _uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            _uavDesc.Buffer.NumElements = elementCount / 4;
            _uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

            //if (!_uav) _uav = Render::Heaps->Reserved.Allocate();
            _resource->SetName(name.data());

            //m_ElementCount = NumElements;
            //m_ElementSize = ElementSize;
            //m_BufferSize = NumElements * ElementSize;

            //D3D12_RESOURCE_DESC ResourceDesc = DescribeBuffer();

            //m_UsageState = D3D12_RESOURCE_STATE_COMMON;

            //D3D12_HEAP_PROPERTIES HeapProps;
            //HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            //HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            //HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            //HeapProps.CreationNodeMask = 1;
            //HeapProps.VisibleNodeMask = 1;

            //ASSERT_SUCCEEDED(g_Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE,
            //                                                   &ResourceDesc, m_UsageState, nullptr, MY_IID_PPV_ARGS(&m_pResource)));

            //m_GpuVirtualAddress = m_pResource->GetGPUVirtualAddress();

            //if (initialData)
            //    CommandContext::InitializeBuffer(*this, initialData, m_BufferSize);

            //if (m_UAV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
            //m_UAV = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            //Render::Device->CreateUnorderedAccessView(Get(), nullptr, &uavDesc, _uav.GetCpuHandle());
            //_resource->SetName(name.data());
            //g_Device->CreateUnorderedAccessView(m_pResource.Get(), nullptr, &UAVDesc, m_UAV);
        }
    };

    class StructuredBuffer final : public GpuBuffer {
        ByteAddressBuffer _counterBuffer;

    public:
        void Create(wstring_view name, uint32 elementSize, uint32 elementCount) {
            _desc = CD3DX12_RESOURCE_DESC::Buffer(elementSize * elementCount, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

            _srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            _srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Buffer.NumElements = elementCount;
            _srvDesc.Buffer.StructureByteStride = elementSize;
            _srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            if (!_srv) _srv = Render::Heaps->Reserved.Allocate();
            //if (m_SRV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
            //m_SRV = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            //g_Device->CreateShaderResourceView(m_pResource.Get(), &SRVDesc, m_SRV);

            _uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            _uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            _uavDesc.Buffer.CounterOffsetInBytes = 0;
            _uavDesc.Buffer.NumElements = elementCount;
            _uavDesc.Buffer.StructureByteStride = elementSize;
            _uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(Render::Device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &_desc,
                _state,
                nullptr,
                IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())
            ));

            _resource->SetName(name.data());

            //_counterBuffer.Create(L"StructuredBuffer::Counter", 1, 4);

            //if (m_UAV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
            //m_UAV = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    };

    class PixelBuffer : public GpuResource {
    public:
        uint64 GetWidth() const { return _desc.Width; }
        uint64 GetHeight() const { return _desc.Height; }
        uint64 GetPitch() const { return _desc.Width * sizeof(uint32); }
        DXGI_FORMAT GetFormat() const { return _desc.Format; }

        bool IsMultisampled() const { return _desc.SampleDesc.Count > 1; }

        // Copies a MSAA source into a non-sampled buffer
        void ResolveFromMultisample(ID3D12GraphicsCommandList* commandList, PixelBuffer& src) {
            if (!src.IsMultisampled())
                throw std::exception("Source must be multisampled");

            src.Transition(commandList, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            Transition(commandList, D3D12_RESOURCE_STATE_RESOLVE_DEST);
            commandList->ResolveSubresource(Get(), 0, src.Get(), 0, src._desc.Format);
        }
    };

    // GPU 2D Texture resource
    class Texture2D final : public PixelBuffer {
    public:
        Texture2D() = default;

        Texture2D(ComPtr<ID3D12Resource> resource) {
            _resource = std::move(resource);
            _desc = _resource->GetDesc();
        }

        // Uploads a resource with no mip-maps. Intended for use with low res textures.
        void Load(DirectX::ResourceUploadBatch& batch,
                  const void* data,
                  int width, int height,
                  wstring name,
                  DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
            assert(data);
            auto mips = width == 64 && height == 64 ? 7 : 1; // enable mips on standard level textures
            _desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, (UINT16)mips);
            _srvDesc.Format = _desc.Format;
            _srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Texture2D.MostDetailedMip = 0;
            _srvDesc.Texture2D.MipLevels = _desc.MipLevels;

            D3D12_SUBRESOURCE_DATA upload = {};
            upload.pData = data;
            upload.RowPitch = GetWidth() * 4;
            upload.SlicePitch = upload.RowPitch * GetHeight();

            if (!_resource)
                CreateOnDefaultHeap(name);

            auto resource = _resource.Get();
            batch.Transition(resource, _state, D3D12_RESOURCE_STATE_COPY_DEST);
            batch.Upload(resource, 0, &upload, 1);
            batch.Transition(resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            _state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            if (mips > 1)
                batch.GenerateMips(resource);
        }

        void Create(int width, int height, wstring name, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
            CreateNoHeap(width, height, format);
            CreateOnDefaultHeap(name, nullptr);
            _state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        void CreateNoHeap(int width, int height, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
            _desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1);
            _srvDesc.Format = _desc.Format;
            _srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Texture2D.MostDetailedMip = 0;
            _srvDesc.Texture2D.MipLevels = _desc.MipLevels;
        }

        // Returns the small placement alignment size in bytes
        D3D12_RESOURCE_ALLOCATION_INFO GetPlacementAlignment(ID3D12Device* device) {
            _desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
            auto info = device->GetResourceAllocationInfo(0, 1, &_desc);
            if (info.Alignment != D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT) {
                // If the alignment requested is not granted, then let D3D tell us
                // the alignment that needs to be used for these resources.
                _desc.Alignment = 0;
                info = device->GetResourceAllocationInfo(0, 1, &_desc);
            }

            return info;
        }

        bool LoadDDS(DirectX::ResourceUploadBatch& batch, filesystem::path path) {
            ThrowIfFailed(DirectX::CreateDDSTextureFromFile(Render::Device, batch, path.c_str(), _resource.ReleaseAndGetAddressOf()));
            _state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; // CreateDDS transitions state
            //Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            batch.Transition(_resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            _desc = _resource->GetDesc();
            return true;
        }

        // this creates a new texture resource on the default heap in copy_dest state, but hasn't copied anything to it.
        void LoadDDS(ID3D12Device* device, const filesystem::path& path, Ptr<uint8[]>& data, List<D3D12_SUBRESOURCE_DATA>& subresources) {
            ThrowIfFailed(DirectX::LoadDDSTextureFromFile(device, path.c_str(), &_resource, data, subresources));
            _resource->SetName(path.c_str());
            _state = D3D12_RESOURCE_STATE_COPY_DEST;
        }
    };

    // Color buffer for render targets or compute shaders
    class ColorBuffer : public PixelBuffer {
        uint32 _sampleCount = 0;

    public:
        Color ClearColor = { 0, 0, 0, 1 };

        void Create(wstring name, uint width, uint height, DXGI_FORMAT format, int samples = 1) {
            _sampleCount = samples;

            _desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1, samples);
            _desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if (samples == 1)
                _desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            D3D12_CLEAR_VALUE clearValue = {};
            clearValue.Format = format;
            memcpy(clearValue.Color, ClearColor, sizeof(ClearColor));

            CreateOnDefaultHeap(name, &clearValue);

            _rtvDesc.Format = format;
            _rtvDesc.ViewDimension = samples == 1 ? D3D12_RTV_DIMENSION_TEXTURE2D : D3D12_RTV_DIMENSION_TEXTURE2DMS;

            _srvDesc.ViewDimension = samples == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE2DMS;
            _srvDesc.Texture2D.MipLevels = 1;
            _srvDesc.Texture2D.MostDetailedMip = 0;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            //AddShaderResourceView(&srvDesc);
        }

        void Clear(ID3D12GraphicsCommandList* cmdList) const {
            DirectX::TransitionResource(cmdList, Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmdList->ClearRenderTargetView(_rtv.GetCpuHandle(), ClearColor, 0, nullptr);
        }
    };

    class DepthBuffer : public PixelBuffer {
        DescriptorHandle _dsv, _roDescriptor;
        D3D12_DEPTH_STENCIL_VIEW_DESC _dsvDesc = {};

    public:
        float StencilDepth = 1.0f;

        void Create(wstring name, UINT width, UINT height, DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT, UINT samples = 1) {
            CD3DX12_HEAP_PROPERTIES depthHeapProperties(D3D12_HEAP_TYPE_DEFAULT);

            _desc = CD3DX12_RESOURCE_DESC::Tex2D(
                format,
                width,
                height,
                1, // This depth stencil view has only one texture.
                1, // Use a single mipmap level.
                samples
            );
            _desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            _state = D3D12_RESOURCE_STATE_DEPTH_WRITE;

            D3D12_CLEAR_VALUE clearValue = {};
            clearValue.Format = format;
            clearValue.DepthStencil.Depth = StencilDepth;
            clearValue.DepthStencil.Stencil = 0;

            ThrowIfFailed(Render::Device->CreateCommittedResource(
                &depthHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &_desc,
                _state,
                &clearValue,
                IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())
            ));

            _resource->SetName(name.c_str());

            _dsvDesc.Format = format;
            _dsvDesc.ViewDimension = samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;

            _srvDesc.ViewDimension = samples == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE2DMS;
            _srvDesc.Texture2D.MipLevels = 1;
            _srvDesc.Texture2D.MostDetailedMip = 0;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Format = DXGI_FORMAT_R32_FLOAT;

            AddDepthView();
            AddShaderResourceView();
        }

        void Clear(ID3D12GraphicsCommandList* commandList) {
            //assert(_state == D3D12_RESOURCE_STATE_DEPTH_WRITE);
            Transition(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            commandList->ClearDepthStencilView(_dsv.GetCpuHandle(), D3D12_CLEAR_FLAG_DEPTH, StencilDepth, 0, 0, nullptr);
        }

        auto GetDSV() const { return _dsv.GetCpuHandle(); }
        //auto GetReadOnlyDSV() { return _roDescriptor.GetCpuHandle(); }

    private:
        void AddDepthView() {
            if (!_dsv)
                _dsv = Render::Heaps->DepthStencil.Allocate();

            Render::Device->CreateDepthStencilView(_resource.Get(), &_dsvDesc, _dsv.GetCpuHandle());
            assert(_dsv.GetCpuHandle().ptr);

            //if (!_roDescriptor)
            //    _roDescriptor = Render::Heaps->DepthStencil.Allocate();

            //auto ro = _dsvDesc;
            //ro.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
            //Render::Device->CreateDepthStencilView(_resource.Get(), &ro, _roDescriptor.GetCpuHandle());
        }
    };

    class RenderTarget : public PixelBuffer {
    public:
        Color ClearColor;

        // Creates a RTV for a swap chain buffer
        void Create(wstring name, IDXGISwapChain* swapChain, UINT buffer, DXGI_FORMAT format) {
            ThrowIfFailed(swapChain->GetBuffer(buffer, IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())));

            _desc = _resource->GetDesc();
            _resource->SetName(name.c_str());

            _rtvDesc.Format = format;
            _rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            AddRenderTargetView();
        }

        // Creates a render target on the default heap
        void Create(wstring name, UINT width, UINT height, DXGI_FORMAT format, const Color& clearColor = { 0, 0, 0 }, UINT samples = 1) {
            ClearColor = clearColor;

            _desc = CD3DX12_RESOURCE_DESC::Tex2D(
                format,
                width,
                height,
                1, // This render target view has only one texture.
                1, // Use a single mipmap level
                samples
            );

            _desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if (samples == 1)
                _desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            _state = D3D12_RESOURCE_STATE_RENDER_TARGET;

            D3D12_CLEAR_VALUE clearValue = {};
            clearValue.Format = format;
            memcpy(clearValue.Color, clearColor, sizeof(float) * 4);

            // Create on default hep
            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(Render::Device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &_desc,
                _state,
                &clearValue,
                IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())
            ));

            _resource->SetName(name.c_str());

            _rtvDesc.Format = format;
            _rtvDesc.ViewDimension = samples > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
            AddRenderTargetView();

            _srvDesc.ViewDimension = samples == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE2DMS;
            _srvDesc.Texture2D.MipLevels = 1;
            _srvDesc.Texture2D.MostDetailedMip = 0;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            AddShaderResourceView();
        }

        //void SetAsRenderTarget(ID3D12GraphicsCommandList* commandList, Inferno::DepthBuffer* depthBuffer = nullptr) {
        //    Transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);

        //    if (depthBuffer)
        //        commandList->OMSetRenderTargets(1, &_rtv, false, &depthBuffer->GetDSV());
        //    else
        //        commandList->OMSetRenderTargets(1, &_rtv, false, nullptr);
        //}

        //void Clear(ID3D12GraphicsCommandList* commandList) {
        //    assert(_state == D3D12_RESOURCE_STATE_RENDER_TARGET);
        //    commandList->ClearRenderTargetView(_rtv.GetCpuHandle(), ClearColor, 0, nullptr);
        //}
    };
}
