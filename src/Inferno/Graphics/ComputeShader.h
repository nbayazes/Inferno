#pragma once
#include "Buffers.h"
#include "Compiler.h"

namespace Inferno {
    // Divides an integral value and rounds up to the nearest alignment.
    template<class T>
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

        void Load(const filesystem::path& file, string entryPoint = "main") {
            LoadComputeShader(file, _rootSignature, _pso, entryPoint);
            if (!_rootSignature || !_pso)
                throw Exception(fmt::format("Unable to load compute shader:\n{}", file.string()));
        }

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
