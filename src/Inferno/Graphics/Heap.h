#pragma once

#include "DirectX.h"
#include "Types.h"

namespace Inferno::Render {
    extern ID3D12Device* Device;
    extern bool HighRes;
}

namespace Inferno {
    struct DescriptorHandle {
        DescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE cpu = {}, D3D12_GPU_DESCRIPTOR_HANDLE gpu = {})
            : _cpuHandle(cpu), _gpuHandle(gpu) {}

        bool IsShaderVisible() const { return _gpuHandle.ptr; }
        operator bool() const { return _cpuHandle.ptr; }
        const CD3DX12_CPU_DESCRIPTOR_HANDLE* operator&() const { return &_cpuHandle; }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle() const { return _cpuHandle; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle() const { return _gpuHandle; }

        DescriptorHandle Offset(int index, uint descriptorSize) {
            auto copy = *this;
            if (copy._cpuHandle.ptr) copy._cpuHandle.Offset(index, descriptorSize);
            if (copy._gpuHandle.ptr) copy._gpuHandle.Offset(index, descriptorSize);
            return copy;
        }

    private:
        CD3DX12_CPU_DESCRIPTOR_HANDLE _cpuHandle;
        CD3DX12_GPU_DESCRIPTOR_HANDLE _gpuHandle;
    };

    //struct ShaderHeapDesc : public D3D12_DESCRIPTOR_HEAP_DESC {
    //    ShaderHeapDesc(uint32 capacity) {
    //        Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    //        NumDescriptors = capacity;
    //        Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    //        NodeMask = 1;
    //    }
    //};

    class UserDescriptorHeap {
        D3D12_DESCRIPTOR_HEAP_DESC _desc = {};
        ComPtr<ID3D12DescriptorHeap> _heap;
        DescriptorHandle _start = {};
        uint32 _descriptorSize = 0;
        uint _index = 0;
        std::mutex _indexLock;
    public:
        UserDescriptorHeap(uint capacity, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible = true) {
            if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
                shaderVisible = false;

            _desc.Type = type;
            _desc.NumDescriptors = capacity;
            _desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            _desc.NodeMask = 1;

            Create();
        }

        auto Size() const { return _desc.NumDescriptors; }
        auto Heap() { return _heap.Get(); }
        auto DescriptorSize() const { return _descriptorSize; }

        // Gets a specific handle by index.
        DescriptorHandle GetHandle(int index) {
            if (index >= (int)Size() || index < 0)
                throw Exception("Out of space in descriptor range");

            return _start.Offset(index, _descriptorSize);
        }

        DescriptorHandle operator[](int index) { return GetHandle(index); }

        void SetName(const wstring& name) { _heap->SetName(name.c_str()); };

        // Returns an unused handle. This ignores any direct index usage.
        DescriptorHandle Allocate(uint count = 1) {
            std::scoped_lock lock(_indexLock);
            auto index = _index;
            _index += count;
            return GetHandle(index);
        }
    private:
        void Create() {
            ThrowIfFailed(Render::Device->CreateDescriptorHeap(&_desc, IID_PPV_ARGS(_heap.ReleaseAndGetAddressOf())));
            _descriptorSize = Render::Device->GetDescriptorHandleIncrementSize(_desc.Type);
            //m_NumFreeDescriptors = _desc.NumDescriptors;
            _start = { _heap->GetCPUDescriptorHandleForHeapStart(), _heap->GetGPUDescriptorHandleForHeapStart() };
        }
    };

    class ShaderVisibleHeap {
        D3D12_DESCRIPTOR_HEAP_DESC _desc = {};
    public:
        ShaderVisibleHeap(uint32 capacity, D3D12_DESCRIPTOR_HEAP_TYPE type) {
            _desc.Type = type;
            _desc.NumDescriptors = capacity;
            _desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            _desc.NodeMask = 1;
        }
    };

    // stride is the number of indices to allocate at once
    template<uint TStride = 1>
    class DescriptorRange {
        UserDescriptorHeap& _heap;
        const uint _start, _size;
        uint _index = 0;
        std::mutex _indexLock;
        List<bool> _free;

    public:
        DescriptorRange(UserDescriptorHeap& heap, uint size, uint offset = 0)
            : _heap(heap), _start(offset), _size(size), _free((size - offset) / TStride) {
            assert(offset + size <= heap.Size());
            SPDLOG_INFO("Created heap with offset: {} and size: {}", offset, size);
            std::fill(_free.begin(), _free.end(), true);
        }

        // Allocates consecutive indices based on TStride
        uint AllocateIndex() {
            return FindFreeIndex();
        }

        uint GetFreeDescriptors() {
            uint i = 0;
            for (auto free : _free)
                i += free;

            SPDLOG_INFO("Free descriptors: {}", i);
            return i;
        }

        void FreeIndex(uint index) {
            //SPDLOG_INFO("Freeing index {}", index);
            std::scoped_lock lock(_indexLock);
            auto stridedIndex = (index - _start) / TStride;
            if (stridedIndex >= _free.size()) throw IndexOutOfRangeException();
            //assert(_free[stridedIndex] == false);
            _free[stridedIndex] = true;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE AllocateCpuHandle() {
            auto index = AllocateIndex();
            return GetCpuHandle(index);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE AllocateGpuHandle() {
            auto index = AllocateIndex();
            return GetGpuHandle(index);
        }

        DescriptorHandle Allocate() {
            auto index = AllocateIndex();
            return GetHandle(index);
        }

        DescriptorHandle GetHandle(uint index) { return _heap.GetHandle(_start + index); };
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint index) {
            return _heap.GetHandle(_start + index).GetGpuHandle();
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(uint index) {
            return _heap.GetHandle(_start + index).GetCpuHandle();
        }

        size_t GetSize() const { return _size; };
        auto DescriptorSize() const { return _heap.DescriptorSize(); }

    private:
        uint FindFreeIndex() {
            std::scoped_lock lock(_indexLock);
            for (uint i = 0; i < _free.size(); i++) {
                if (_free[i]) {
                    _free[i] = false;
                    //if (index < _index)
                        //SPDLOG_WARN("Wrapped descriptor range index");

                    _index = i;
                    auto newIndex = _start + i * TStride;
                    assert(newIndex >= _start && newIndex < _start + _size);
                    //SPDLOG_INFO("Allocating index {}", newIndex);
                    return newIndex;
                }
            }

            SPDLOG_ERROR("No free indices in descriptor range!");
            throw Exception("No free indices in descriptor range!");
        }
    };

    class DescriptorHeaps {
        UserDescriptorHeap _shader;
    public:
        DescriptorHeaps(uint capacity, uint reserved, uint renderTargets = 10)
            : _shader(capacity, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
            States(Render::Device),
            Reserved(_shader, reserved),
            Shader(_shader, capacity - reserved, reserved),
            RenderTargets(renderTargets, D3D12_DESCRIPTOR_HEAP_TYPE_RTV),
            DepthStencil(5, D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {

            _shader.SetName(L"Shader visible heap");
            RenderTargets.SetName(L"Render target heap");
            DepthStencil.SetName(L"Depth stencil heap");
        }

        DirectX::CommonStates States;
        // Static CBV SRV UAV for buffers
        DescriptorRange<1> Reserved;
        // Dynamic CBV SRV UAV for shader texture resources
        DescriptorRange<4> Shader;
        UserDescriptorHeap RenderTargets, DepthStencil;

        void SetDescriptorHeaps(ID3D12GraphicsCommandList* cmdList) {
            ID3D12DescriptorHeap* heaps[] = { _shader.Heap(), States.Heap() };
            cmdList->SetDescriptorHeaps((uint)std::size(heaps), heaps);
        }
    };

    namespace Render {
        inline Ptr<DescriptorHeaps> Heaps;
    }
}
