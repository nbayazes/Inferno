#include "pch.h"
#include "GpuResources.h"
#include "Render.h"

namespace Inferno {
    void GpuResource::CreateShaderResourceView(D3D12_CPU_DESCRIPTOR_HANDLE dest, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc) const {
        Render::Device->CreateShaderResourceView(Get(), desc, dest);
    }

    // If desc is null then default initialization is used. Not supported for all resources.
    void GpuResource::CreateUnorderedAccessView(D3D12_CPU_DESCRIPTOR_HANDLE dest, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc)  const {
        Render::Device->CreateUnorderedAccessView(Get(), nullptr, desc, dest);
    }

    void GpuResource::Create(D3D12_HEAP_TYPE heapType, wstring_view name, const D3D12_CLEAR_VALUE* clearValue) {
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

        _resource->SetName(name.data());
        _name = name;
    }
}
