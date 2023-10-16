#include "pch.h"
#include <filesystem>
#include <dxcapi.h>
#include "Types.h"
#include "Compiler.h"
#include "Render.h"
#include "Logging.h"

namespace Inferno {
    namespace {
        ComPtr<IDxcUtils> Utils;
        ComPtr<IDxcCompiler3> Compiler;
        ComPtr<IDxcIncludeHandler> IncludeHandler;
    }

    void LogComException(const com_exception& e, ID3DBlob* error) {
        SPDLOG_ERROR(e.what());
        if (error) {
            auto size = error->GetBufferSize();
            auto* msgs = error->GetBufferPointer();
            std::string msg((const char*)msgs, size);
            SPDLOG_ERROR(msg);
        }
    }

    // Load Root Signature from the shader (must be defined in hlsl)
    void LoadShaderRootSig(ID3DBlob& shader, ComPtr<ID3D12RootSignature>& rootSignature) {
        ThrowIfFailed(Inferno::Render::Device->CreateRootSignature(
            0,
            shader.GetBufferPointer(),
            shader.GetBufferSize(),
            IID_PPV_ARGS(&rootSignature)
        ));
    }

    // Load Root Signature from the shader (must be defined in hlsl)
    void LoadShaderRootSig(IDxcBlob& shader, ComPtr<ID3D12RootSignature>& rootSignature) {
        ThrowIfFailed(Inferno::Render::Device->CreateRootSignature(
            0,
            shader.GetBufferPointer(),
            shader.GetBufferSize(),
            IID_PPV_ARGS(&rootSignature)
        ));
    }

    std::filesystem::path GetBinaryPath(const std::filesystem::path& file, const std::string& ext) {
        auto name = file.stem().string() + ext;
        return file.parent_path() / "bin" / name;
    }

    void CheckCompilerResult(IDxcResult* result/*, const std::filesystem::path& file*/) {
        ComPtr<IDxcBlobUtf8> pErrors;
        ThrowIfFailed(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(pErrors.GetAddressOf()), nullptr));
        if (pErrors && pErrors->GetStringLength() > 0) {
            throw Inferno::Exception((char*)pErrors->GetBufferPointer());
        }
    }

    void AddCommonArgs(std::vector<LPCWSTR>& args, LPCWSTR entryPoint, LPCWSTR profile) {
        args.push_back(L"-E"); // Entrypoint
        args.push_back(entryPoint);

        args.push_back(L"-T"); // Target profile
        args.push_back(profile);

        args.push_back(L"-I"); // Include directory
        args.push_back(L"shaders");

        //args.push_back(L"-no-warnings"); // warnings are grouped with errors and cause compilation to abort

        // -Fd pdb path to helper debuggers

        args.push_back(L"-Qstrip_debug");
        args.push_back(L"-Qstrip_reflect");

        args.push_back(L"-Zi"); // Debug, profiling
#if defined(_DEBUG)
        args.push_back(DXC_ARG_OPTIMIZATION_LEVEL0);
#endif
    }

    void LoadFile(const filesystem::path& file, ComPtr<ID3DBlob>& result) {
        uint32_t codePage = CP_UTF8;
        ComPtr<IDxcBlobEncoding> sourceBlob;
        ThrowIfFailed(Utils->LoadFile(file.c_str(), &codePage, &sourceBlob));
        ThrowIfFailed(sourceBlob.As(&result));
    }

    void CompileShader(const filesystem::path& file, span<LPCWSTR> args, ComPtr<ID3DBlob>& result) {
        uint32_t codePage = CP_UTF8;
        ComPtr<IDxcBlobEncoding> source;
        ThrowIfFailed(Utils->LoadFile(file.c_str(), &codePage, &source));

        DxcBuffer sourceBuffer{};
        sourceBuffer.Ptr = source->GetBufferPointer();
        sourceBuffer.Size = source->GetBufferSize();

        ComPtr<IDxcResult> dxcResult;
        ThrowIfFailed(Compiler->Compile(&sourceBuffer, args.data(), (uint32)args.size(), IncludeHandler.Get(), IID_PPV_ARGS(&dxcResult)));
        CheckCompilerResult(dxcResult.Get());

        ComPtr<IDxcBlob> resultBlob;
        ThrowIfFailed(dxcResult->GetResult(&resultBlob));
        ThrowIfFailed(resultBlob.As(&result));
    }

    ComPtr<ID3DBlob> LoadComputeShader(const filesystem::path& file, ComPtr<ID3D12RootSignature>& rootSignature, ComPtr<ID3D12PipelineState>& pso, wstring entryPoint) {
        ComPtr<ID3DBlob> shader;

        auto binaryPath = GetBinaryPath(file, ".bin");
        if (std::filesystem::exists(binaryPath)) {
            SPDLOG_INFO(L"Loading compute shader {}", binaryPath.wstring());
            LoadFile(binaryPath, shader);
        }
        else {
            SPDLOG_INFO(L"Compiling compute shader {}:{}", file.wstring(), entryPoint);
            List<LPCWSTR> args;
            AddCommonArgs(args, entryPoint.c_str(), L"cs_6_0");
            CompileShader(file, args, shader);
        }

        LoadShaderRootSig(*shader.Get(), rootSignature);

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = rootSignature.Get();
        psoDesc.CS.pShaderBytecode = shader->GetBufferPointer();
        psoDesc.CS.BytecodeLength = shader->GetBufferSize();

        ThrowIfFailed(rootSignature->SetName(file.c_str()));
        ThrowIfFailed(Render::Device->CreateComputePipelineState(
            &psoDesc,
            IID_PPV_ARGS(pso.ReleaseAndGetAddressOf())
        ));
        ThrowIfFailed(pso->SetName(file.c_str()));

        return shader;
    }

    ComPtr<ID3DBlob> LoadVertexShader(const filesystem::path& file, ComPtr<ID3D12RootSignature>& rootSignature, wstring entryPoint) {
        ComPtr<ID3DBlob> shader;

        auto binaryPath = GetBinaryPath(file, ".vs.bin");
        if (filesystem::exists(binaryPath)) {
            SPDLOG_INFO(L"Loading vertex shader {}", binaryPath.wstring());
            LoadFile(binaryPath, shader);
        }
        else {
            if (!filesystem::exists(file))
                throw Exception(fmt::format("Shader file not found:\n{}", file.string()));

            SPDLOG_INFO(L"Compiling vertex shader {}:{}", file.wstring(), entryPoint);

            List<LPCWSTR> args;
            AddCommonArgs(args, entryPoint.c_str(), L"vs_6_0");
            CompileShader(file, args, shader);
        }

        // load root sig from the shader hlsl
        LoadShaderRootSig(*shader.Get(), rootSignature);
        ThrowIfFailed(rootSignature->SetName(file.c_str()));

        return shader;
    }

    ComPtr<ID3DBlob> LoadPixelShader(const filesystem::path& file, wstring entryPoint) {
        ComPtr<ID3DBlob> shader;
        auto binaryPath = GetBinaryPath(file, ".ps.bin");
        if (filesystem::exists(binaryPath)) {
            SPDLOG_INFO(L"Loading pixel shader {}", binaryPath.wstring());
            LoadFile(binaryPath, shader);
        }
        else {
            SPDLOG_INFO(L"Compiling pixel shader {}:{}", file.wstring(), entryPoint);

            List<LPCWSTR> args;
            AddCommonArgs(args, entryPoint.c_str(), L"ps_6_0");
            CompileShader(file, args, shader);
        }

        return shader;
    }

    void InitShaderCompiler() {
        try {
            ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&Utils)));
            ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&Compiler)));
            ThrowIfFailed(Utils->CreateDefaultIncludeHandler(&IncludeHandler));
        }
        catch (...) {
            SPDLOG_ERROR("Error creating DXC compiler");
        }
    }
}
