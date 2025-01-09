#pragma once
#include "Buffers.h"
#include "Compiler.h"

namespace Inferno {
    // Divides an integral value and rounds up to the nearest alignment.
    template <class T>
    constexpr T AlignedCeil(T value, T alignment) {
        return (value + alignment - 1) / alignment;
    }

    class ComputeShader {
    protected:
        ComPtr<ID3D12PipelineState> _pso;
        ComPtr<ID3D12RootSignature> _rootSignature;
        UINT _numThreadsX, _numThreadsY;

    public:
        ComputeShader(UINT numThreadsX, UINT numThreadsY)
            : _numThreadsX(numThreadsX), _numThreadsY(numThreadsY) {}

        void Load(const filesystem::path& file, string_view entryPoint = "main") {
            if (!std::filesystem::exists(file)) {
                auto msg = fmt::format("Shader {} not found", file.string());
                SPDLOG_ERROR(msg);
                throw std::exception(msg.c_str()); // never initialized, crash
            }

            try {
                LoadComputeShader(file, _rootSignature, _pso, entryPoint);
            }
            catch (const std::exception& e) {
                SPDLOG_ERROR(e.what());
                if (!_pso || !_rootSignature) {
                    auto msg = fmt::format("Unable to compile {}\n\n{}", file.string(), e.what());
                    throw std::exception(msg.c_str()); // never initialized, crash
                }
            }
        }

    protected:
        void Dispatch2D(ID3D12GraphicsCommandList* commandList, UINT width, UINT height) const {
            auto threadGroupsX = AlignedCeil(width, _numThreadsX);
            auto threadGroupsY = AlignedCeil(height, _numThreadsY);
            commandList->Dispatch(threadGroupsX, threadGroupsY, 1);
        }

        // Dispatches thread groups based on the resource dimensions
        void Dispatch2D(ID3D12GraphicsCommandList* commandList, const PixelBuffer& resource) const {
            Dispatch2D(commandList, (UINT)resource.GetWidth(), (UINT)resource.GetHeight());
        }
    };
}
