#pragma once

#include "Heap.h"
#include "FileSystem.h"
#include "Resources.h"
#include "GpuResources.h"

namespace Inferno {
    inline const D3D12_RANGE CPU_READ_NONE = {};
    inline const D3D12_RANGE* CPU_READ_ALL = nullptr;

    constexpr uint Align(uint location, uint align = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) {
        return (location + (align - 1)) & ~(align - 1);
    }

    // Creates a buffer upload heap
    inline void CreateUploadHeap(ComPtr<ID3D12Resource>& resource, uint64 bufferSize) {
        auto props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        ThrowIfFailed(Render::Device->CreateCommittedResource(
            &props,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())
        ));
    }

    inline void CreateOnDefaultHeap(ComPtr<ID3D12Resource>& resource, const D3D12_RESOURCE_DESC& desc) {
        auto props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(Render::Device->CreateCommittedResource(
            &props,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(resource.ReleaseAndGetAddressOf())
        ));
    }

    // Creates a buffer default heap
    inline void CreateOnDefaultHeap(ComPtr<ID3D12Resource>& resource, uint64 bufferSize) {
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        CreateOnDefaultHeap(resource, desc);
    }

    // Packs multiple vertex and index buffers into a single buffer.
    // Packed buffers use GraphicsResource which automatically upload at end of frame.
    class PackedBuffer {
        uint _index = 0;
        uint _size;
        DirectX::GraphicsResource _resource;

    public:
        PackedBuffer(uint size = 1024 * 1024 * 10)
            : _size(size) {
            _resource = DirectX::GraphicsMemory::Get().Allocate(size);
        }

        void ResetIndex() { _index = 0; }

        // Aligns offset to a stride
        constexpr uint Stride(uint offset, uint stride) {
            return (offset + stride - 1) / stride * stride;
        }

        template<class TVertex>
        D3D12_VERTEX_BUFFER_VIEW PackVertices(List<TVertex> data) {
            constexpr auto stride = sizeof(TVertex);
            auto size = uint(data.size() * stride);
            if (_index + size > _size) throw Exception("Ran out of space in GPU buffer");
            memcpy((byte*)_resource.Memory() + _index, data.data(), size);

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = _resource.GpuAddress() + _index;
            vbv.SizeInBytes = size;
            vbv.StrideInBytes = stride;

            _index += size;
            _index = Stride(_index, 4); // ensure stride of 4 to prevent issues on AMD
            return vbv;
        }

        template<class TIndex = uint16>
        D3D12_INDEX_BUFFER_VIEW PackIndices(List<TIndex> data) {
            constexpr auto stride = sizeof(TIndex);
            static_assert(stride == 2 || stride == 4);
            constexpr auto format = stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

            auto size = uint(data.size() * stride);
            if (_index + size > _size) throw Exception("Ran out of space in GPU buffer");
            memcpy((byte*)_resource.Memory() + _index, data.data(), size);

            D3D12_INDEX_BUFFER_VIEW ibv{};
            ibv.BufferLocation = _resource.GpuAddress() + _index;
            ibv.SizeInBytes = size;
            ibv.Format = format;
            _index += size;
            _index = Stride(_index, 4); // ensure stride of 4 to prevent issues on AMD
            return ibv;
        }
    };

    //class RingBuffer {
    //    struct FrameResource {
    //        uint Frame;
    //        uint8* ResourceOffset;
    //    };

    //    Queue<FrameResource> _frameQueue;
    //    uint _size;

    //public:
    //    void Update() {}

    //    //void Suballocate(ID3D12CommandQueue* cmdQueue, uint size, uint alignment) {
    //    //    // check memory
    //    //
    //    //};
    //};

    //template<class T>
    //class DynamicBuffer {
    //    bool _mapped = false;
    //public:
    //    ComPtr<ID3D12Resource> Resource;

    //    void Map(void* data) {
    //        // technically resources support nested mapping, but unsure when you'd want to do so
    //        if (_mapped) throw Exception("Buffer is already mapped");
    //        ThrowIfFailed(Resource->Map(0, &CPU_READ_NONE, &data));
    //        _mapped = true;
    //    }

    //    bool Copy(List<T>& src) {
    //        /*if (src.size() > Size) return false;
    //        memcpy(data, src.data(), src.size() * sizeof(T));
    //        return true;*/
    //    }

    //    void Unmap() {
    //        Resource->Unmap(0, &CPU_READ_NONE);
    //        _mapped = false;

    //        // transition copy state
    //    }

    //    /*void CopyTo(Buffer<T> buffer) {

    //    }*/
    //};

    /* Buffer to allocate shader constants for each draw call. Stays mapped for its lifespan.

       Usage:
       DrawConstantBuffer* drawCB = (DrawConstantBuffer*)m_dynamicCB.GetMappedMemory(m_drawIndex, m_frameIndex);
       drawCB->worldViewProjection = ...
       m_commandList->SetGraphicsRootConstantBufferView(RootParameterCB, m_dynamicCB.GetGpuVirtualAddress(m_drawIndex, m_frameIndex));
    */
    class DynamicConstantBuffer {
        ComPtr<ID3D12Resource> _buffer;
        void* m_pMappedConstantBuffer;
        uint  m_alignedPerDrawConstantBufferSize;
        uint  m_perFrameConstantBufferSize;

        uint m_frameCount;
        uint m_maxDrawsPerFrame;
    public:
        DynamicConstantBuffer(uint constantSize, uint maxDrawsPerFrame, uint frameCount) :
            m_alignedPerDrawConstantBufferSize(Align(constantSize)), // Constant buffers must be aligned for hardware requirements.
            m_maxDrawsPerFrame(maxDrawsPerFrame),
            m_frameCount(frameCount),
            _buffer(nullptr) {
            m_perFrameConstantBufferSize = m_alignedPerDrawConstantBufferSize * m_maxDrawsPerFrame;
        }

        ~DynamicConstantBuffer() {
            _buffer->Unmap(0, nullptr);
        }

        DynamicConstantBuffer(const DynamicConstantBuffer&) = delete;
        DynamicConstantBuffer(DynamicConstantBuffer&&) = default;
        DynamicConstantBuffer& operator=(const DynamicConstantBuffer&) = delete;
        DynamicConstantBuffer& operator=(DynamicConstantBuffer&&) = default;

        void Init() {
            const UINT bufferSize = m_perFrameConstantBufferSize * m_frameCount;
            CreateUploadHeap(_buffer, bufferSize);
            _buffer->SetName(L"Dynamic constant buffer");
            ThrowIfFailed(_buffer->Map(0, &CPU_READ_NONE, &m_pMappedConstantBuffer));
        }

        void* GetMappedMemory(uint drawIndex, uint frameIndex) {
            assert(drawIndex < m_maxDrawsPerFrame);
            uint constantBufferOffset = (frameIndex * m_perFrameConstantBufferSize) + (drawIndex * m_alignedPerDrawConstantBufferSize);
            return (uint8*)m_pMappedConstantBuffer + constantBufferOffset;
        }

        D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress(uint drawIndex, uint frameIndex) {
            uint constantBufferOffset = (frameIndex * m_perFrameConstantBufferSize) + (drawIndex * m_alignedPerDrawConstantBufferSize);
            return _buffer->GetGPUVirtualAddress() + constantBufferOffset;
        }
    };

    // Fixed size upload heap buffer
    template<class T>
    struct Buffer {
        ComPtr<ID3D12Resource> Resource;
        uint Size;

        void Resize(ID3D12Device* device, uint size) {
            Size = size;
            CreateUploadHeap(device, Resource, size * sizeof(T));
        }

        void Fill(ID3D12Device* device, span<T> src) {
            if (src.size() > Size) Resize(device, (uint)src.size() * 3 / 2);
            void* data;
            ThrowIfFailed(Resource->Map(0, &CPU_READ_NONE, &data));
            memcpy(data, src.data(), src.size() * sizeof(T));
            Resource->Unmap(0, &CPU_READ_NONE);
        }
    };

    // Buffer for packing indices and vertices into a single buffer
    class PackedUploadBuffer {
        uint _offset = 0; // offset in bytes into the buffer
        uint _size;
        GpuBuffer _resource;

        byte* _mappedData = nullptr;

    public:
        PackedUploadBuffer(uint size = 1024 * 1024 * 10)
            : _size(size), _resource(size) {
            _resource.CreateOnUploadHeap(L"Upload buffer");
            ThrowIfFailed(_resource->Map(0, &CPU_READ_NONE, (void**)&_mappedData));
        }

        void Reset() { _offset = 0; }

        template<class TVertex>
        D3D12_VERTEX_BUFFER_VIEW PackVertices(List<TVertex> data) {
            constexpr auto stride = sizeof(TVertex);
            auto size = uint(data.size() * stride);
            if (_offset + size > _size) throw Exception("Ran out of space in buffer");
            memcpy(_mappedData + _offset, data.data(), size);

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = _resource->GetGPUVirtualAddress() + _offset;
            vbv.SizeInBytes = size;
            vbv.StrideInBytes = stride;

            _offset += size;
            return vbv;
        }

        template<class TIndex = uint16>
        D3D12_INDEX_BUFFER_VIEW PackIndices(List<TIndex> data) {
            constexpr auto stride = sizeof(TIndex);
            static_assert(stride == 2 || stride == 4);
            constexpr auto format = stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

            auto size = uint(data.size() * stride);
            if (_offset + size > _size) throw Exception("Ran out of space in buffer");
            memcpy(_mappedData + _offset, data.data(), size);

            D3D12_INDEX_BUFFER_VIEW ibv{};
            ibv.BufferLocation = _resource->GetGPUVirtualAddress() + _offset;
            ibv.SizeInBytes = size;
            ibv.Format = format;
            _offset += size;
            return ibv;
        }
    };

    // Resizable buffer that uses the upload heap every frame.
    // intended for use with small dynamic buffers
    template<class T>
    class UploadBuffer {
        ComPtr<ID3D12Resource> _resource;
        bool _inUpdate = false;
        T* _mappedData = nullptr;
        size_t _gpuCapacity = 0, _requestedCapacity, _gpuElements = 0;
        List<T> _buffer;
    public:
        UploadBuffer(size_t capacity) : _requestedCapacity(capacity) {
            _buffer.reserve(capacity);
        }

        const uint Stride = sizeof(T);
        auto GetGPUVirtualAddress() { return _resource->GetGPUVirtualAddress(); }
        uint GetSizeInBytes() { return (uint)(sizeof(T) * _gpuCapacity); }
        uint GetElementCount() { return (uint)_gpuElements; }

        void Begin() {
            if (_inUpdate) throw Exception("Already called Begin");
            _inUpdate = true;

            if (!_resource || _requestedCapacity > _gpuCapacity) {
                _gpuCapacity = size_t(_requestedCapacity * 1.5);
                CreateUploadHeap(_resource, _gpuCapacity * sizeof(T));

                //if (_mapped) _resource->Unmap(0, &CPU_READ_NONE);
                ThrowIfFailed(_resource->Map(0, &CPU_READ_NONE, (void**)&_mappedData));
            }

            _buffer.clear();
        }

        // returns false if too much data is in the buffer to copy
        bool End() {
            if (!_inUpdate) throw Exception("Must call Begin before End");
            _inUpdate = false;

            // copy to GPU
            memcpy(_mappedData, _buffer.data(), _buffer.size() * sizeof(T));
            _gpuElements = _buffer.size();
            return true;
        }

        void Copy(span<T> src) {
            if (!_inUpdate) throw Exception("Must call Begin before Copy");
            if (_buffer.size() + src.size() > _gpuCapacity) {
                _requestedCapacity = _buffer.size() + src.size();
                return;
            }

            _buffer.insert(_buffer.end(), src.begin(), src.end());
        }
    };
}
