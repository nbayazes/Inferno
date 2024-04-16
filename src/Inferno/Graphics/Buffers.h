#pragma once

#include "Heap.h"
#include "GpuResources.h"
#include "Utility.h"

namespace Inferno {
    constexpr D3D12_RANGE CPU_READ_NONE = {};
    inline const D3D12_RANGE* CPU_READ_ALL = nullptr;

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
        PackedBuffer(uint size = 1024 * 1024 * 20)
            : _size(size) {
            _resource = DirectX::GraphicsMemory::Get().Allocate(size);
        }

        void ResetIndex() { _index = 0; }

        template <class TVertex>
        D3D12_VERTEX_BUFFER_VIEW PackVertices(span<TVertex> data) {
            constexpr auto stride = sizeof(TVertex);
            auto size = uint(data.size() * stride);
            if (_index + size > _size)
                throw Exception("Ran out of space in GPU buffer");
            memcpy((byte*)_resource.Memory() + _index, data.data(), size);

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = _resource.GpuAddress() + _index;
            vbv.SizeInBytes = size;
            vbv.StrideInBytes = stride;

            _index += size;
            _index = AlignTo(_index, 4); // alignment of 4 to prevent issues on AMD
            return vbv;
        }

        template <class TIndex = uint16>
        D3D12_INDEX_BUFFER_VIEW PackIndices(span<TIndex> data) {
            constexpr auto stride = sizeof(TIndex);
            static_assert(stride == 2 || stride == 4);
            constexpr auto format = stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

            auto size = uint(data.size() * stride);
            if (_index + size > _size)
                throw Exception("Ran out of space in GPU buffer");

            memcpy((byte*)_resource.Memory() + _index, data.data(), size);

            D3D12_INDEX_BUFFER_VIEW ibv{};
            ibv.BufferLocation = _resource.GpuAddress() + _index;
            ibv.SizeInBytes = size;
            ibv.Format = format;
            _index += size;
            _index = AlignTo(_index, 4); // alignment of 4 to prevent issues on AMD
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
        void* _pMappedConstantBuffer = nullptr;
        uint _alignedPerDrawConstantBufferSize;
        uint _perFrameConstantBufferSize;

        uint _frameCount;
        uint _maxDrawsPerFrame;

    public:
        DynamicConstantBuffer(uint constantSize, uint maxDrawsPerFrame, uint frameCount) :
            _buffer(nullptr),
            // Constant buffers must be aligned for hardware requirements.
            _alignedPerDrawConstantBufferSize(AlignTo(constantSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)),
            _frameCount(frameCount),
            _maxDrawsPerFrame(maxDrawsPerFrame) {
            _perFrameConstantBufferSize = _alignedPerDrawConstantBufferSize * _maxDrawsPerFrame;
        }

        ~DynamicConstantBuffer() {
            _buffer->Unmap(0, nullptr);
        }

        DynamicConstantBuffer(const DynamicConstantBuffer&) = delete;
        DynamicConstantBuffer(DynamicConstantBuffer&&) = default;
        DynamicConstantBuffer& operator=(const DynamicConstantBuffer&) = delete;
        DynamicConstantBuffer& operator=(DynamicConstantBuffer&&) = default;

        void Init() {
            const UINT bufferSize = _perFrameConstantBufferSize * _frameCount;
            CreateUploadHeap(_buffer, bufferSize);
            ThrowIfFailed(_buffer->SetName(L"Dynamic constant buffer"));
            ThrowIfFailed(_buffer->Map(0, &CPU_READ_NONE, &_pMappedConstantBuffer));
        }

        void* GetMappedMemory(uint drawIndex, uint frameIndex) const {
            assert(drawIndex < _maxDrawsPerFrame);
            uint constantBufferOffset = (frameIndex * _perFrameConstantBufferSize) + (drawIndex * _alignedPerDrawConstantBufferSize);
            return (uint8*)_pMappedConstantBuffer + constantBufferOffset;
        }

        D3D12_GPU_VIRTUAL_ADDRESS GetGpuVirtualAddress(uint drawIndex, uint frameIndex) const {
            uint constantBufferOffset = (frameIndex * _perFrameConstantBufferSize) + (drawIndex * _alignedPerDrawConstantBufferSize);
            return _buffer->GetGPUVirtualAddress() + constantBufferOffset;
        }
    };

    // Fixed size upload heap buffer
    template <class T>
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
            : _size(size) {
            _resource.CreateOnUploadHeap(L"Upload buffer");
            ThrowIfFailed(_resource->Map(0, &CPU_READ_NONE, (void**)&_mappedData));
        }

        void Reset() { _offset = 0; }

        template <class TVertex>
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

        template <class TIndex = uint16>
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
    template <class T>
    class UploadBuffer {
        ComPtr<ID3D12Resource> _resource;
        bool _inUpdate = false;
        T* _mappedData = nullptr;
        size_t _gpuCapacity = 0, _requestedCapacity, _gpuElements = 0;
        List<T> _buffer;
        DescriptorHandle _srv, _uav;
        bool _forbidResize = false;
        wstring _name;

    public:
        UploadBuffer(size_t capacity, wstring_view name) : _requestedCapacity(capacity), _name(name) {
            _buffer.reserve(capacity);
            _gpuCapacity = _requestedCapacity;
        }

        D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const { return _resource->GetGPUVirtualAddress(); }
        uint GetSizeInBytes() const { return (uint)(sizeof(T) * _gpuCapacity); }
        uint GetElementCount() const { return (uint)_gpuElements; }
        static uint GetStride() { return sizeof(T); }

        const auto GetSRV() const { return _srv.GetGpuHandle(); }
        const auto GetUAV() const { return _uav.GetGpuHandle(); }

        ID3D12Resource* Get() const { return _resource.Get(); }

        // note that these views become invalid if the buffer resizes
        void CreateShaderResourceView() {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.NumElements = (UINT)_buffer.size();
            srvDesc.Buffer.StructureByteStride = GetStride();
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            if (!_srv) _srv = Render::Heaps->Reserved.Allocate();
            Render::Device->CreateShaderResourceView(_resource.Get(), &srvDesc, _srv.GetCpuHandle());
            _forbidResize = true;
        }

        //void CreateUnorderedAccessView() {
        //    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        //    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        //    desc.Format = DXGI_FORMAT_UNKNOWN;
        //    desc.Buffer.CounterOffsetInBytes = 0;
        //    desc.Buffer.NumElements = _buffer.size();
        //    desc.Buffer.StructureByteStride = Stride;
        //    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        //    if (!_uav) _uav = Render::Heaps->Reserved.Allocate();
        //    Render::Device->CreateUnorderedAccessView(_resource.Get(), nullptr, &desc, _uav.GetCpuHandle());
        //}

        void Begin() {
            if (_inUpdate) throw Exception("Already called Begin");
            _inUpdate = true;

            bool shouldGrow = _requestedCapacity > _gpuCapacity;
            if (!_resource || shouldGrow) {
                if (shouldGrow)
                    _gpuCapacity = size_t(_requestedCapacity * 1.5);
                CreateUploadHeap(_resource, _gpuCapacity * sizeof(T));
                std::ignore = _resource->SetName(_name.c_str());

                //if (_mapped) _resource->Unmap(0, &CPU_READ_NONE);
                // leave the buffer mapped
                ThrowIfFailed(_resource->Map(0, &CPU_READ_NONE, (void**)&_mappedData));
            }

            _buffer.clear();
        }

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

        void Copy(T& src) {
            if (!_inUpdate) throw Exception("Must call Begin before Copy");
            if (_buffer.size() + 1 > _gpuCapacity) {
                _requestedCapacity = _buffer.size() + 1;
                return;
            }

            //_buffer.insert(_buffer.end(), src.begin(), src.end());
            _buffer.insert(_buffer.end(), src);
        }
    };

    // Fixed size buffer that uses the upload heap every frame.
    class FrameUploadBuffer {
        ComPtr<ID3D12Resource> _resource;
        size_t _gpuCapacity = 0, _gpuElements = 0;
        uint8* _cpuMemory{};
        D3D12_GPU_VIRTUAL_ADDRESS _gpuMemory{};
        size_t _size;
        std::atomic<int64> _allocated = 0;
        bool _reset = false;

    public:
        FrameUploadBuffer(size_t size) : _size(size) {
            D3D12_RESOURCE_DESC resourceDesc = {};
            resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resourceDesc.Width = size;
            resourceDesc.Height = 1;
            resourceDesc.DepthOrArraySize = 1;
            resourceDesc.MipLevels = 1;
            resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
            resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            resourceDesc.SampleDesc.Count = 1;
            resourceDesc.SampleDesc.Quality = 0;
            resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resourceDesc.Alignment = 0;

            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            ThrowIfFailed(Render::Device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_resource)));
            ThrowIfFailed(_resource->Map(0, &CPU_READ_NONE, reinterpret_cast<void**>(&_cpuMemory)));
            _gpuMemory = _resource->GetGPUVirtualAddress();
        }

        MappedHandle GetMemory(uint64 size, uint64 alignment) {
            uint64 allocSize = size + alignment;
            //uint64 offset = InterlockedAdd64(&_frameCount, allocSize) - allocSize;
            //SPDLOG_INFO("ALLOC: {} ADDR: {}", _allocated.load(), (void*)this);

            if (_reset) {
                ASSERT(_allocated == 0);
                _reset = false;
            }

            uint64 offset = _allocated.fetch_add(allocSize);
            if (alignment > 0)
                offset = AlignTo(offset, alignment);

            if (offset + size > _size)
                throw Exception("Out of memory in frame constant buffer");

            MappedHandle handle;
            handle.CPU = _cpuMemory + offset;
            handle.GPU = _gpuMemory + offset;
            handle.Offset = offset;
            handle.Resource = _resource.Get();
            return handle;
        }

        void ResetIndex() {
            _allocated = 0;
            //SPDLOG_INFO("RESET INDEX: {} ADDR: {}", _allocated.load(), (void*)this);
            _reset = true;
        }
    };
}
