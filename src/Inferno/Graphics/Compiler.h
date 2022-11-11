#pragma once

#include "DirectX.h"
#include <filesystem>

namespace Inferno {
    void LogComException(const com_exception& e, ID3DBlob* error);

    // Load Root Signature from the shader (must be defined in hlsl)
    void LoadShaderRootSig(ID3DBlob& shader, ComPtr<ID3D12RootSignature>& rootSignature);

    // Returns the shader
    ComPtr<ID3DBlob> LoadComputeShader(const std::filesystem::path& file,
                                       ComPtr<ID3D12RootSignature>& rootSignature,
                                       ComPtr<ID3D12PipelineState>& pso,
                                       string entryPoint = "main");

    // Returns the shader
    ComPtr<ID3DBlob> LoadVertexShader(const std::filesystem::path& file,
                                      ComPtr<ID3D12RootSignature>& rootSignature,
                                      string entryPoint = "vsmain");

    // Returns the shader
    ComPtr<ID3DBlob> LoadPixelShader(const std::filesystem::path& file,
                                     string entryPoint = "psmain");
}