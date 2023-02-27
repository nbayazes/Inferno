#pragma once

#define NOMINMAX

#include <wrl.h>
using Microsoft::WRL::ComPtr;

#include <d3d12.h>
#include <DirectXTK12/GamePad.h>
#include <DirectXTK12/GraphicsMemory.h>
#include <DirectXTK12/Keyboard.h>
#include <DirectXTK12/Mouse.h>
#include <DirectXTK12/DescriptorHeap.h>
#include <DirectXTK12/Audio.h>
#include <DirectXTK12/CommonStates.h>
#include <DirectXTK12/DDSTextureLoader.h>
#include <DirectXTK12/ResourceUploadBatch.h>
#include <DirectXTK12/Effects.h>
#include <DirectXTK12/VertexTypes.h>
#include <DirectXTK12/SpriteBatch.h>
#include <DirectXTK12/PrimitiveBatch.h>
#include <DirectXTK12/SimpleMath.h>
#include <DirectXTK12/GeometricPrimitive.h>
#include <DirectXTK12/DirectXHelpers.h>

#include <PlatformHelpers.h>

#include <pix3.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include <d3dx12.h>

constexpr D3D12_GPU_VIRTUAL_ADDRESS D3D12_GPU_VIRTUAL_ADDRESS_NULL = 0;
constexpr auto D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN = D3D12_GPU_VIRTUAL_ADDRESS(-1);

// Assign a name to the object to aid with debugging.
#if defined(_DEBUG) || defined(DBG)
#include <fmt/format.h>

inline void SetName(ID3D12Object* pObject, LPCWSTR name) {
    pObject->SetName(name);
}

inline void SetNameIndexed(ID3D12Object* pObject, LPCWSTR name, UINT index) {
    pObject->SetName(fmt::format(L"{}[{}]", name, index).c_str());
}
#else
inline void SetName(ID3D12Object*, LPCWSTR) {}
inline void SetNameIndexed(ID3D12Object*, LPCWSTR, UINT) {}
#endif

#define NAME_D3D12_OBJECT(x) SetName((x).Get(), L#x)
#define NAME_D3D12_OBJECT_INDEXED(x, n) SetNameIndexed((x)[n].Get(), L#x, n)