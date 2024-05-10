#pragma once

#include "DirectX.h"
#include "Heap.h"
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

        ID3D12Resource* Get() const { return _resource.Get(); }
        ID3D12Resource* operator->() { return _resource.Get(); }
        const ID3D12Resource* operator->() const { return _resource.Get(); }
        operator bool() const { return _resource.Get() != nullptr; }

        void Release() {
            _resource.Reset();
        }

        void Transition(ID3D12GraphicsCommandList* cmdList, D3D12_RESOURCE_STATES state) {
            if (_state == state) return;
            DirectX::TransitionResource(cmdList, _resource.Get(), _state, state);
            _state = state;
        }

        void CreateOnUploadHeap(wstring_view name, const D3D12_CLEAR_VALUE* clearValue = nullptr) {
            Create(D3D12_HEAP_TYPE_UPLOAD, name, clearValue);
        }

        void CreateOnDefaultHeap(wstring_view name, const D3D12_CLEAR_VALUE* clearValue = nullptr) {
            Create(D3D12_HEAP_TYPE_DEFAULT, name, clearValue);
        }

        // If desc is null then default initialization is used. Not supported for all resources.
        void CreateShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE dest, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc = nullptr) const;

        // If desc is null then default initialization is used. Not supported for all resources.
        void CreateUnorderedAccessView(D3D12_CPU_DESCRIPTOR_HANDLE dest, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc = nullptr) const;

        void SetName(wstring_view name) {
            _name = name;
            ThrowIfFailed(_resource->SetName(name.data()));
        }

    private:
        void Create(D3D12_HEAP_TYPE heapType, wstring_view name, const D3D12_CLEAR_VALUE* clearValue = nullptr);
    };

    // General purpose buffer
    class GpuBuffer : public GpuResource {
    public:
        GpuBuffer(uint64 size) {
            _desc = CD3DX12_RESOURCE_DESC::Buffer(size);
            _state = D3D12_RESOURCE_STATE_GENERIC_READ;
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
    };

    class Texture2D final : public PixelBuffer {
        ComPtr<ID3D12Resource> _uploadBuffer; // Only used for CopyTo

    public:
        Texture2D() = default;

        Texture2D(ComPtr<ID3D12Resource> resource) {
            _resource = std::move(resource);
            _desc = _resource->GetDesc();
        }

        // Copies data from another texture into the resource
        void CopyFrom(ID3D12GraphicsCommandList* cmdList, Texture2D& srcTex) {
            CD3DX12_TEXTURE_COPY_LOCATION dst(Get());
            CD3DX12_TEXTURE_COPY_LOCATION src(srcTex.Get());
            srcTex.Transition(cmdList, D3D12_RESOURCE_STATE_COPY_SOURCE);
            Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);

            cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            srcTex.Transition(cmdList, D3D12_RESOURCE_STATE_COMMON);
        }

        // Copies data from a buffer into the resource
        void CopyFrom(ID3D12GraphicsCommandList* cmdList, const void* data) {
            D3D12_SUBRESOURCE_DATA textureData = {};
            textureData.pData = data;
            textureData.RowPitch = GetWidth() * 4;
            textureData.SlicePitch = textureData.RowPitch * GetHeight();

            // Reuse the upload buffer between each call
            if (!_uploadBuffer)
                CreateUploadBuffer();

            //Transition(cmdList, D3D12_RESOURCE_STATE_COMMON);
            Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);
            UpdateSubresources(cmdList, _resource.Get(), _uploadBuffer.Get(), 0, 0, 1, &textureData);
            Transition(cmdList, D3D12_RESOURCE_STATE_COMMON);
        }

        // Uploads a resource with no mip-maps. Intended for use with low res textures.
        void Load(DirectX::ResourceUploadBatch& batch,
                  const void* data,
                  uint width, uint height,
                  wstring_view name,
                  bool enableMips = true,
                  DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
            // This should default to DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but the editor doesn't use SRGB yet

            assert(data);
            auto mips = enableMips && width == 64 && height == 64 ? 7u : 1u; // enable mips on standard level textures
            SetDesc(width, height, (uint16)mips, format);

            uint64 bpp = format == DXGI_FORMAT_R8_UNORM ? 1 : 4;

            D3D12_SUBRESOURCE_DATA upload = {};
            upload.pData = data;
            upload.RowPitch = GetWidth() * bpp;
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

        // Creates the texture on the default heap
        void Create(uint width, uint height, wstring_view name, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            SetDesc(width, height, 1, format);
            CreateOnDefaultHeap(name, nullptr);
            _state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        // Sets the SRV description
        void SetDesc(uint width, uint height, uint16 mips = 1, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            _desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, mips);
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

        bool LoadDDS(DirectX::ResourceUploadBatch& batch, const filesystem::path& path, bool srgb = false) {
            if (!filesystem::exists(path)) {
                SPDLOG_WARN("File not found: {}", path.string());
                return false;
            }

            auto loadFlags = srgb ? DirectX::DDS_LOADER_FORCE_SRGB : DirectX::DDS_LOADER_DEFAULT;

            ThrowIfFailed(DirectX::CreateDDSTextureFromFileEx(Render::Device, batch, path.c_str(), 0, D3D12_RESOURCE_FLAG_NONE, loadFlags, _resource.ReleaseAndGetAddressOf()));
            _state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; // CreateDDS transitions state
            //Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            batch.Transition(_resource.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            _desc = _resource->GetDesc();

            _srvDesc.Format = _desc.Format;
            _srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Texture2D.MostDetailedMip = 0;
            _srvDesc.Texture2D.MipLevels = _desc.MipLevels;
            return true;
        }

        // this creates a new texture resource on the default heap in copy_dest state, but hasn't copied anything to it.
        void LoadDDS(ID3D12Device* device, const filesystem::path& path, Ptr<uint8[]>& data, List<D3D12_SUBRESOURCE_DATA>& subresources) {
            ThrowIfFailed(DirectX::LoadDDSTextureFromFile(device, path.c_str(), &_resource, data, subresources));
            SetName(_name);
            _state = D3D12_RESOURCE_STATE_COPY_DEST;
        }

    private:
        void CreateUploadBuffer() {
            const uint64 uploadBufferSize = GetRequiredIntermediateSize(_resource.Get(), 0, 1);
            auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
            CD3DX12_HEAP_PROPERTIES props(D3D12_HEAP_TYPE_UPLOAD);

            ThrowIfFailed(Render::Device->CreateCommittedResource(
                &props,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&_uploadBuffer)));
        }
    };

    // Color buffer for render targets or compute shaders
    class ColorBuffer : public PixelBuffer {
        uint32 _fragmentCount{}, _sampleCount{};

    public:
        Color ClearColor;

        void Create(wstring name, uint width, uint height, DXGI_FORMAT format, int samples = 1) {
            _sampleCount = samples;

            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

            _desc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height);
            _desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            _desc.MipLevels = 1;
            if (samples == 1)
                _desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            //D3D12_CLEAR_VALUE optimizedClearValue = {};
            //optimizedClearValue.Format = format;
            //memcpy(optimizedClearValue.Color, ClearColor, sizeof(ClearColor));
            CreateOnDefaultHeap(name);

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
        DescriptorHandle _descriptor;
        friend class RenderTarget;
        D3D12_DEPTH_STENCIL_VIEW_DESC _dsvDesc = {};

    public:
        float ClearDepth = 1.0f;

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
            clearValue.DepthStencil.Depth = ClearDepth;
            clearValue.DepthStencil.Stencil = 0;

            ThrowIfFailed(Render::Device->CreateCommittedResource(
                &depthHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &_desc,
                _state,
                &clearValue,
                IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())
            ));

            SetName(name);

            _dsvDesc.Format = format;
            _dsvDesc.ViewDimension = samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;

            _srvDesc.ViewDimension = samples == 1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE2DMS;
            _srvDesc.Texture2D.MipLevels = 1;
            _srvDesc.Texture2D.MostDetailedMip = 0;
            _srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            _srvDesc.Format = DXGI_FORMAT_R32_FLOAT;

            AddDepthView();
        }

        void Clear(ID3D12GraphicsCommandList* commandList) {
            //assert(_state == D3D12_RESOURCE_STATE_DEPTH_WRITE);
            Transition(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            commandList->ClearDepthStencilView(_descriptor.GetCpuHandle(), D3D12_CLEAR_FLAG_DEPTH, ClearDepth, 0, 0, nullptr);
        }

        auto GetDSV() { return _descriptor.GetCpuHandle(); }

    private:
        void AddDepthView() {
            if (!_descriptor)
                _descriptor = Render::Heaps->DepthStencil.Allocate();

            Render::Device->CreateDepthStencilView(_resource.Get(), &_dsvDesc, _descriptor.GetCpuHandle());
        }
    };

    class RenderTarget : public PixelBuffer {
    public:
        Color ClearColor;

        // Creates a RTV for a swap chain buffer
        void Create(wstring_view name, IDXGISwapChain* swapChain, UINT buffer, DXGI_FORMAT format) {
            ThrowIfFailed(swapChain->GetBuffer(buffer, IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())));

            _desc = _resource->GetDesc();
            SetName(name);

            _rtvDesc.Format = format;
            _rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            AddRenderTargetView();
        }

        // Creates a render target on the default heap
        void Create(wstring_view name, UINT width, UINT height, DXGI_FORMAT format, const Color& clearColor = { 0, 0, 0 }, UINT samples = 1) {
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

            D3D12_CLEAR_VALUE optimizedClearValue = {};
            optimizedClearValue.Format = format;
            memcpy(optimizedClearValue.Color, clearColor, sizeof(float) * 4);

            // Create on default hep
            CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
            ThrowIfFailed(Render::Device->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &_desc,
                _state,
                &optimizedClearValue,
                IID_PPV_ARGS(_resource.ReleaseAndGetAddressOf())
            ));

            SetName(name);

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

        bool IsMultisampled() const { return _desc.SampleDesc.Count > 1; }


        // Copies a MSAA source into a non-sampled buffer
        void ResolveFromMultisample(ID3D12GraphicsCommandList* commandList, RenderTarget& src) {
            if (!src.IsMultisampled())
                throw std::exception("Source must be multisampled");

            src.Transition(commandList, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            Transition(commandList, D3D12_RESOURCE_STATE_RESOLVE_DEST);
            commandList->ResolveSubresource(Get(), 0, src.Get(), 0, src._desc.Format);
        }

        //void SetAsRenderTarget(ID3D12GraphicsCommandList* commandList, Inferno::DepthBuffer* depthBuffer = nullptr) {
        //    Transition(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);

        //    if (depthBuffer)
        //        commandList->OMSetRenderTargets(1, &_rtv.GetCpuHandle(), false, &depthBuffer->_descriptor.GetCpuHandle());
        //    else
        //        commandList->OMSetRenderTargets(1, &_rtv, false, nullptr);
        //}

        //void Clear(ID3D12GraphicsCommandList* commandList) {
        //    assert(_state == D3D12_RESOURCE_STATE_RENDER_TARGET);
        //    commandList->ClearRenderTargetView(_rtv.GetCpuHandle(), ClearColor, 0, nullptr);
        //}
    };
}
