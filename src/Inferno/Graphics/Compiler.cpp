#include "pch.h"
#include "Types.h"
#include "Compiler.h"
#include "Render.h"
#include "logging.h"
#include <D3Dcompiler.h>
#include <filesystem>

#if defined(_DEBUG)
// Enable better shader debugging with the graphics debugging tools.
constexpr UINT COMPILE_FLAGS = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
constexpr UINT COMPILE_FLAGS = 0;
#endif

void Inferno::LogComException(const com_exception& e, ID3DBlob* error) {
    SPDLOG_ERROR(e.what());
    if (error) {
        auto size = error->GetBufferSize();
        auto* msgs = error->GetBufferPointer();
        string msg((const char*)msgs, size);
        SPDLOG_ERROR(msg);
    }
}

// Load Root Signature from the shader (must be defined in hlsl)
void Inferno::LoadShaderRootSig(ID3DBlob& shader, ComPtr<ID3D12RootSignature>& rootSignature) {
    ThrowIfFailed(Render::Device->CreateRootSignature(
        0,
        shader.GetBufferPointer(),
        shader.GetBufferSize(),
        IID_PPV_ARGS(&rootSignature)
    ));
}

std::filesystem::path GetBinaryPath(std::filesystem::path file, std::string ext) {
    auto name = file.stem().string() + ext;
    return file.parent_path() / "bin" / name;
}

ComPtr<ID3DBlob> CreateBlobFromBytes(std::span<char> src) {
    ComPtr<ID3DBlob> blob;
    ThrowIfFailed(D3DCreateBlob(src.size(), &blob)); // allocate D3DBlob
    memcpy(blob->GetBufferPointer(), src.data(), src.size()); // Copy shader data
    return blob;
}

ComPtr<ID3DBlob> Inferno::LoadComputeShader(const filesystem::path& file, ComPtr<ID3D12RootSignature>& rootSignature, ComPtr<ID3D12PipelineState>& pso, string entryPoint) {
    ComPtr<ID3DBlob> shader, error;
    try {
        auto binaryPath = GetBinaryPath(file, ".bin");
        if (std::filesystem::exists(binaryPath)) {
            SPDLOG_INFO(L"Loading compute shader {}", binaryPath.wstring());
            ThrowIfFailed(D3DReadFileToBlob(binaryPath.c_str(), &shader));
        }
        else {
            SPDLOG_INFO(L"Compiling compute shader {}:{}", file.wstring(), Convert::ToWideString(entryPoint));
            
            ThrowIfFailed(D3DCompileFromFile(
                file.c_str(), nullptr,
                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entryPoint.c_str(),
                "cs_5_1", COMPILE_FLAGS,
                0, &shader, &error
            ));
        }

        LoadShaderRootSig(*shader.Get(), rootSignature);
        rootSignature->SetName(file.c_str());

        D3D12_COMPUTE_PIPELINE_STATE_DESC descComputePSO = {};
        descComputePSO.pRootSignature = rootSignature.Get();
        descComputePSO.CS.pShaderBytecode = shader->GetBufferPointer();
        descComputePSO.CS.BytecodeLength = shader->GetBufferSize();

        ThrowIfFailed(Render::Device->CreateComputePipelineState(
            &descComputePSO,
            IID_PPV_ARGS(pso.ReleaseAndGetAddressOf())
        ));
        pso->SetName(file.c_str());
    }
    catch (const com_exception& e) {
        LogComException(e, error.Get());
    }

    return shader;
}

ComPtr<ID3DBlob> Inferno::LoadVertexShader(const filesystem::path& file, ComPtr<ID3D12RootSignature>& rootSignature, string entryPoint) {
    ComPtr<ID3DBlob> shader, error;
    try {
        auto binaryPath = GetBinaryPath(file, ".vs.bin");
        if (filesystem::exists(binaryPath)) {
            SPDLOG_INFO(L"Loading vertex shader {}", binaryPath.wstring());
            ThrowIfFailed(D3DReadFileToBlob(binaryPath.c_str(), &shader));
        }
        else {
            if (!filesystem::exists(file))
                throw Exception(fmt::format("Shader file not found:\n{}", file.string()));

            SPDLOG_INFO(L"Compiling vertex shader {}:{}", file.wstring(), Convert::ToWideString(entryPoint));
            ThrowIfFailed(D3DCompileFromFile(
                file.c_str(), 
                nullptr, 
                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entryPoint.c_str(),
                "vs_5_1", COMPILE_FLAGS,
                0, &shader, &error));
        }

        // load root sig from the shader hlsl
        LoadShaderRootSig(*shader.Get(), rootSignature);
        rootSignature->SetName(file.c_str());
    }
    catch (const com_exception& e) {
        LogComException(e, error.Get());
    }
    catch (const Exception& e) {
        SPDLOG_ERROR(e.what());
    }

    return shader;
}

ComPtr<ID3DBlob> Inferno::LoadPixelShader(const filesystem::path& file, string entryPoint) {
    ComPtr<ID3DBlob> shader, error;
    try {
        auto binaryPath = GetBinaryPath(file, ".ps.bin");
        if (filesystem::exists(binaryPath)) {
            SPDLOG_INFO(L"Loading pixel shader {}", binaryPath.wstring());
            ThrowIfFailed(D3DReadFileToBlob(binaryPath.c_str(), &shader));
        }
        else {
            SPDLOG_INFO(L"Compiling pixel shader {}:{}", file.wstring(), Convert::ToWideString(entryPoint));
            ThrowIfFailed(D3DCompileFromFile(
                file.c_str(), 
                nullptr, 
                D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entryPoint.c_str(),
                "ps_5_1", COMPILE_FLAGS,
                0, &shader, &error));
        }
    }
    catch (const com_exception& e) {
        LogComException(e, error.Get());
    }

    return shader;
}
