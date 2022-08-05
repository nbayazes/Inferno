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
    public:
        GpuResource() = default;

        virtual ~GpuResource() = default;
        GpuResource(const GpuResource&) = delete;
        GpuResource(GpuResource&&) = default;
        GpuResource& operator=(const GpuResource&) = delete;
        GpuResource& operator=(GpuResource&&) = default;

        ID3D12Resource* Get() { return _resource.Get(); }
        ID3D12Resource* operator->() { return _resource.Get(); }
        const ID3D12Resource* operator->() const { return _resource.Get(); }
        operator bool() { return _resource.Get() != nullptr; }
        void Release() {
            _resource.Reset();
        }

        void Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES state) {
            if (_state == state) return;
            DirectX::TransitionResource(cmdList, _resource.Get(), _state, state);

            //if (state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            //    D3D12_RESOURCE_BARRIER barrier{};
            //    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            //    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            //    barrier.UAV.pResource = _resource.Get();
            //    cmdList->ResourceBarrier(1, &barrier);
            //}
            _state = state;
        }

        void CreateOnUploadHeap(wstring name, D3D12_CLEAR_VALUE* clearValue = nullptr) {
            Create(D3D12_HEAP_TYPE_UPLOAD, name, clearValue);
        }

        void CreateOnDefaultHeap(wstring name, D3D12_CLEAR_VALUE* clearValue = nullptr) {
            Create(D3D12_HEAP_TYPE_DEFAULT, name, clearValue);
        }

        // If desc is null then default initialization is used. Not supported for all resources.
        void CreateShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE dest, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc = nullptr) {
            Render::Device->CreateShaderResourceView(Get(), desc, dest);
        }

        // If desc is null then default initialization is used. Not supported for all resources.
        void CreateUnorderedAccessView(D3D12_CPU_DESCRIPTOR_HANDLE dest, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc = nullptr) {
            Render::Device->CreateUnorderedAccessView(Get(), nullptr, desc, dest);
        }

    private:
        void Create(D3D12_HEAP_TYPE heapType, wstring name, D3D12_CLEAR_VALUE* clearValue) {
            _heapType = heapType;
            CD3DX12_HEAP_PROPERTIES props(_heapType);
            ThrowIfFailed(
                Render::Device->CreateCommittedResource(
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
        DescriptorHandle _srv;
        D3D12_SHADER_RESOURCE_VIEW_DESC _srvDesc{};
    public:
        GpuBuffer(uint64 size) {
            _desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        }

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


    class PixelBuffer : public GpuResource {
    protected:
        DescriptorHandle _srv, _rtv, _uav;
        D3D12_RENDER_TARGET_VIEW_DESC _rtvDesc = {};
        D3D12_SHADER_RESOURCE_VIEW_DESC _srvDesc = {};
        D3D12_UNORDERED_ACCESS_VIEW_DESC _uavDesc = {};

    public:
        uint64 GetWidth() const { return _desc.Width; }
        uint64 GetHeight() const { return _desc.Height; }
        uint64 GetPitch() const { return _desc.Width * sizeof(uint32); }
        DXGI_FORMAT GetFormat() const { return _desc.Format; }

        const auto GetSRV() const { return _srv.GetGpuHandle(); }
        const auto GetUAV() const { return _uav.GetGpuHandle(); }
        const auto GetRTV() const { return _rtv.GetCpuHandle(); }

        void AddShaderResourceView() {
            assert(Get()); // Call CreateOnUploadHeap or CreateOnDefaultHeap first
            if (!_srv) _srv = Render::Heaps->Reserved.Allocate();
            Render::Device->CreateShaderResourceView(Get(), &_srvDesc, _srv.GetCpuHandle());
        }

        void AddUnorderedAccessView() {
            assert(Get()); // Call CreateOnUploadHeap or CreateOnDefaultHeap first
            if (!_uav) _uav = Render::Heaps->Reserved.Allocate();
            auto desc = _uavDesc.Format == DXGI_FORMAT_UNKNOWN ? nullptr : &_uavDesc;
            Render::Device->CreateUnorderedAccessView(Get(), nullptr, desc, _uav.GetCpuHandle());
        }

        void AddRenderTargetView() {
            assert(Get()); // Call CreateOnUploadHeap or CreateOnDefaultHeap first
            if (!_rtv) _rtv = Render::Heaps->RenderTargets.Allocate();
            Render::Device->CreateRenderTargetView(Get(), &_rtvDesc, _rtv.GetCpuHandle());
        }

        bool IsMultisampled() { return _desc.SampleDesc.Count > 1; }

        // Copies a MSAA source into a non-sampled buffer
        void ResolveFromMultisample(ID3D12GraphicsCommandList* commandList, PixelBuffer& src) {
            if (!src.IsMultisampled())
                throw std::exception("Source must be multisampled");

            src.Transition(commandList, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            Transition(commandList, D3D12_RESOURCE_STATE_RESOLVE_DEST);
            commandList->ResolveSubresource(Get(), 0, src.Get(), 0, src._desc.Format);
        }
    };

    class Texture2D : public PixelBuffer {
    public:
        Texture2D() = default;

        // Uploads a resource with no mip-mapping. Intended for use with low res textures.
        void Load(DirectX::ResourceUploadBatch& batch,
                  const void* data,
                  int width, int height,
                  wstring name,
                  DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
            assert(data);
            //auto mips = width == 64 && height == 64 ? 7 : 1;
            _desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1);
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
                CreateOnDefaultHeap(name, nullptr);

            auto resource = _resource.Get();
            batch.Transition(resource, _state, D3D12_RESOURCE_STATE_COPY_DEST);
            batch.Upload(resource, 0, &upload, 1);
            batch.Transition(resource, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            _state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            //batch.GenerateMips(resource);
        }

        void Create(int width, int height,
                    wstring name,
                    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {

            _desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1);
            _srvDesc.Format = _desc.Format;
            _srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Texture2D.MostDetailedMip = 0;
            _srvDesc.Texture2D.MipLevels = _desc.MipLevels;

            CreateOnDefaultHeap(name, nullptr);
            _state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        Tuple<Ptr<uint8[]>, List<D3D12_SUBRESOURCE_DATA>> CreateFromDDS(filesystem::path path) {
            Ptr<uint8[]> data;
            List<D3D12_SUBRESOURCE_DATA> subres;
            // this creates a new texture resource on the default heap in copy_dest state, but hasn't copied anything to it.
            ThrowIfFailed(DirectX::LoadDDSTextureFromFile(Render::Device, path.c_str(), _resource.ReleaseAndGetAddressOf(), data, subres));
            _state = D3D12_RESOURCE_STATE_COPY_DEST;
            return { std::move(data), std::move(subres) };
        }

        bool LoadDDS(DirectX::ResourceUploadBatch& batch, filesystem::path path) {
            ThrowIfFailed(DirectX::CreateDDSTextureFromFile(Render::Device, batch, path.c_str(), _resource.ReleaseAndGetAddressOf()));
            _state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; // CreateDDS transitions state
            //Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            batch.Transition(_resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            _desc = _resource->GetDesc();
            return true;
        }
    };

    // Color buffer for render targets or compute shaders
    class ColorBuffer : public PixelBuffer {
        uint32 _fragmentCount, _sampleCount;

    public:
        Color ClearColor = { 0, 0, 0, 1 };

        void Create(wstring name, uint width, uint height, DXGI_FORMAT format, int samples = 1) {
            _sampleCount = samples;

            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

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

            //D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            _srvDesc.ViewDimension = samples == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE2DMS;
            _srvDesc.Texture2D.MipLevels = 1;
            _srvDesc.Texture2D.MostDetailedMip = 0;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            //AddShaderResourceView(&srvDesc);
        }

        // Creates a buffer for use with unordered access
        void CreateUnorderedAccess(wstring name, uint size,
                                   DXGI_FORMAT format = DXGI_FORMAT_R32_TYPELESS,
                                   D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
                                   UINT64 alignment = 0) {
            _desc = CD3DX12_RESOURCE_DESC::Buffer(size, flags, alignment);
            _state = D3D12_RESOURCE_STATE_GENERIC_READ;

            _srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            _srvDesc.Format = format;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Buffer.NumElements = (UINT)size / 4;
            _srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

            _uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            _uavDesc.Format = format;
            _uavDesc.Buffer.NumElements = (UINT)size / 4;
            _uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        }

        void Clear(ID3D12GraphicsCommandList* cmdList) {
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

        auto GetDSV() { return _dsv.GetCpuHandle(); }
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

            if (!_srv) _srv = Render::Heaps->Reserved.Allocate();
            Render::Device->CreateShaderResourceView(Get(), &_srvDesc, _srv.GetCpuHandle());
            //AddShaderResourceView();
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
