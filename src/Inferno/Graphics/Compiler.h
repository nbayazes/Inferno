#pragma once

#include "DirectX.h"
#include <filesystem>

namespace Inferno {
    // Returns the shader
    ComPtr<ID3DBlob> LoadComputeShader(const std::filesystem::path& file,
                                       ComPtr<ID3D12RootSignature>& rootSignature,
                                       ComPtr<ID3D12PipelineState>& pso,
                                       string_view entryPoint = "main");

    // Returns the shader
    ComPtr<ID3DBlob> LoadVertexShader(const std::filesystem::path& file,
                                      ComPtr<ID3D12RootSignature>& rootSignature,
                                      string_view entryPoint = "vsmain");

    // Returns the shader
    ComPtr<ID3DBlob> LoadPixelShader(const std::filesystem::path& file,
                                     string_view entryPoint = "psmain");

    void InitShaderCompiler();
}