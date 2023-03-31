#pragma once

#include "DirectX.h"
#include <filesystem>

namespace Inferno {
    // Returns the shader
    ComPtr<ID3DBlob> LoadComputeShader(const std::filesystem::path& file,
                                       ComPtr<ID3D12RootSignature>& rootSignature,
                                       ComPtr<ID3D12PipelineState>& pso,
                                       wstring entryPoint = L"main");

    // Returns the shader
    ComPtr<ID3DBlob> LoadVertexShader(const std::filesystem::path& file,
                                      ComPtr<ID3D12RootSignature>& rootSignature,
                                      wstring entryPoint = L"vsmain");

    // Returns the shader
    ComPtr<ID3DBlob> LoadPixelShader(const std::filesystem::path& file,
                                     wstring entryPoint = L"psmain");

    void InitShaderCompiler();
}