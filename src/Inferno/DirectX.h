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

#include <vendor/PlatformHelpers.h>

// To use graphics and CPU markup events with the latest version of PIX, change this to include <pix3.h>
// then add the NuGet package WinPixEventRuntime to the project.
#include <pix.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include <vendor/d3dx12.h>

constexpr D3D12_GPU_VIRTUAL_ADDRESS D3D12_GPU_VIRTUAL_ADDRESS_NULL = 0;
constexpr auto D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN = D3D12_GPU_VIRTUAL_ADDRESS(-1);
