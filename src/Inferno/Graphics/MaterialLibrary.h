#pragma once

#include "Heap.h"
#include "Level.h"
#include "Resources.h"
#include "Buffers.h"
#include "Concurrent.h"
#include "FileSystem.h"
#include "ScopedTimer.h"

namespace Inferno::Render {
    constexpr void FillTexture(span<ubyte> data, ubyte red, ubyte green, ubyte blue, ubyte alpha) {
        for (size_t i = 0; i < data.size() / 4; i++) {
            data[i * 4] = red;
            data[i * 4 + 1] = green;
            data[i * 4 + 2] = blue;
            data[i * 4 + 3] = alpha;
        }
    }

    struct Material2D {
        enum { Diffuse, SuperTransparency, Emissive, Specular, Count };

        Texture2D Textures[Count]{};
        // SRV handles
        D3D12_GPU_DESCRIPTOR_HANDLE Handles[Count] = {};
        uint Index = 0;
        TexID ID = TexID::Invalid;
        string Name;
    };

    struct MaterialUpload {
        TexID ID = TexID::None;
        Outrage::Bitmap Outrage;
        const PigBitmap* Bitmap;
        bool SuperTransparent = false;
        bool ForceLoad = false;
    };

    // Supports loading and unloading materials
    class MaterialLibrary {
        Material2D _defaultMaterial;

        bool _requestPrune = false;
        Texture2D _black, _white, _purple;
        ConcurrentList<Material2D> _materials, PendingCopies;
        ConcurrentList<MaterialUpload> RequestedUploads;
        Dictionary<string, Material2D> _unpackedMaterials;

        Ptr<WorkerThread> _worker;
        friend class MaterialUploadWorker;
    public:
        MaterialLibrary(size_t size);

        void Shutdown();
        Material2D White, Black;

        void LoadMaterials(span<const TexID> tids, bool forceLoad);
        void LoadMaterialsAsync(span<const TexID> ids, bool forceLoad = false);
        void Dispatch();

        // Gets a material based on a D1/D2 texture ID
        const Material2D& Get(TexID id) const {
            if ((int)id > _materials.Size()) return _defaultMaterial;
            auto& material = _materials[(int)id];
            return material.ID > TexID::Invalid ? material : _defaultMaterial;
        }

        // Gets a material based on a D1/D2 level texture ID
        const Material2D& Get(LevelTexID tid) const {
            auto id = Resources::LookupTexID(tid);
            return Get(id);
        }

        // Gets a material loaded from the filesystem based on name
        const Material2D& Get(const string& name) {
            if (!_unpackedMaterials.contains(name)) return _defaultMaterial;
            return _unpackedMaterials[name];
        };

        void LoadLevelTextures(const Inferno::Level& level, bool force);
        void LoadTextures(span<string> names);

        void Reload();

        bool PreloadDoors = true; // For editor previews

        // Unloads unused materials
        void Prune() { _requestPrune = true; }
        void Unload();

        // Materials to keep loaded after a prune
        Set<TexID> KeepLoaded;

    private:
        MaterialUpload PrepareUpload(TexID id, bool forceLoad);
        void PruneInternal();

        bool HasUnloadedTextures(span<const TexID> tids) {
            bool hasPending = false;
            for (auto& id : tids) {
                if (id <= TexID::Invalid) continue;
                if (_materials[(int)id].ID == id) continue;
                hasPending = true;
                break;
            }

            return hasPending;
        }

        void LoadDefaults();
    };

    DirectX::ResourceUploadBatch BeginTextureUpload();

    ComPtr<ID3D12CommandQueue> EndTextureUpload(DirectX::ResourceUploadBatch&);

    List<TexID> GetTexturesForModel(ModelID id);
    Set<TexID> GetLevelTextures(const Level& level, bool preloadDoors);

    inline Ptr<MaterialLibrary> Materials;

    class MaterialLibrary2 {
        //Material2D _defaultMaterial;
        List<Material2D> _materials; // materials indexed by TexID

        bool _requestPrune = false;
        Texture2D _black, _white, _purple;
        Dictionary<string, Material2D> _nameLookup;

        List<ComPtr<ID3D12Resource>> _intermediates;

        ComPtr<ID3D12CommandAllocator> m_copyCommandAllocator;
        ComPtr<ID3D12CommandQueue> m_copyQueue;
        ComPtr<ID3D12GraphicsCommandList> m_copyCommandList;

        ComPtr<ID3D12Heap> _heap;
        size_t _heapSize;
        size_t _allocated = 0;

        ID3D12Device* _device;

        struct TextureUpload {
            Ptr<uint8[]> data{}; // For DDS textures until uploaded
            List<D3D12_SUBRESOURCE_DATA> subresources{}; // used by both
            Texture2D Texture; // destination
            bool OnHeap = false; // Indicates if the upload has been placed on the heap. DDS textures load onto heap automatically.
            CD3DX12_RESOURCE_BARRIER Barrier{}; // Aliasing barrier
        };

        struct MaterialUpload {
            Array<Option<TextureUpload>, 4> Textures{};
            TexID ID = TexID::None;
            string Name;
        };
    public:
        MaterialLibrary2(ID3D12Device* device, size_t heapSize) : _heapSize(heapSize), _device(device) {
            _materials.resize(3000);
            ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_copyCommandAllocator)));
            ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, m_copyCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_copyCommandList)));
            ThrowIfFailed(m_copyCommandList->Close());
            NAME_D3D12_OBJECT(m_copyCommandList);

            D3D12_COMMAND_QUEUE_DESC queueDesc = { .Type = D3D12_COMMAND_LIST_TYPE_COPY };
            ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_copyQueue)));
            NAME_D3D12_OBJECT(m_copyQueue);

            CD3DX12_HEAP_DESC heapDesc(heapSize, D3D12_HEAP_TYPE_DEFAULT, 0, D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES);
            ThrowIfFailed(device->CreateHeap(&heapDesc, IID_PPV_ARGS(&_heap)));

            LoadDefaults();
        }

        Material2D& GetMaterial(TexID id) {
            return _materials[(int)id];
        }

        void LoadMaterials(span<const TexID> tids, bool forceLoad) {
            if (!forceLoad && !HasUnloadedTextures(tids)) return;
            List<MaterialUpload> uploads;
            uploads.reserve(tids.size());

            for (auto& id : tids) {
                if (id <= TexID::Invalid) continue;
                if (_materials[(int)id].ID == id && !forceLoad) continue; // already loaded
                auto& ti = Resources::GetTextureInfo(id);
                uploads.push_back(LoadMaterial(id, ti.Name));
            }

            LoadMaterials(uploads);
        }

        void WaitOnCopyQueue() const {
            // Set an event so we get notified when the GPU has completed all its work
            ComPtr<ID3D12Fence> fence;
            ThrowIfFailed(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_GRAPHICS_PPV_ARGS(fence.GetAddressOf())));
            NAME_D3D12_OBJECT(fence);

            HANDLE gpuCompletedEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
            if (!gpuCompletedEvent)
                throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()), "CreateEventEx");

            // Wait for uploads to finish
            ThrowIfFailed(m_copyQueue->Signal(fence.Get(), 1ULL));
            ThrowIfFailed(fence->SetEventOnCompletion(1ULL, gpuCompletedEvent));
            WaitForSingleObjectEx(gpuCompletedEvent, INFINITE, FALSE);
        }

        void LoadTextures(const List<TextureUpload>& uploads) {
            Stopwatch time;

            {
                // populate command list to copy textures to GPU
                ThrowIfFailed(m_copyCommandList->Reset(m_copyCommandAllocator.Get(), nullptr));

                {
                    // Alias each placed texture in the heap
                    std::vector<D3D12_RESOURCE_BARRIER> barriers;
                    barriers.reserve(uploads.size());

                    for (auto& texture : uploads) {
                        if (texture.OnHeap) continue;
                        barriers.push_back(texture.Barrier);
                    }

                    m_copyCommandList->ResourceBarrier((uint)barriers.size(), barriers.data());
                }

                // Create upload buffers for each texture and copy it over
                for (auto& upload : uploads) {
                    CopyToGpu(upload);
                }

                // Create views for each material, setting defaults (white/black/unknown)
                ThrowIfFailed(m_copyCommandList->Close());

                ID3D12CommandList* ppCommandLists[] = { m_copyCommandList.Get() };
                m_copyQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
            }

            WaitOnCopyQueue();

            _intermediates.clear();

            //// Wait for the copy queue to complete execution of the command list.
            //m_copyQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]);
            //ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
            //WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

            SPDLOG_INFO("LoadTextures(): {:.3f}s", time.GetElapsedSeconds());
            //Render::Adapter->PrintMemoryUsage();
            Render::Heaps->Shader.GetFreeDescriptors();
        }

        void LoadMaterials(const List<MaterialUpload>& uploads) {
            Stopwatch time;

            int statsPlaced = 0, statsCommitted = 0;

            // populate command list to copy textures to GPU
            ThrowIfFailed(m_copyCommandList->Reset(m_copyCommandAllocator.Get(), nullptr));

            {
                // Alias each placed texture in the heap
                std::vector<D3D12_RESOURCE_BARRIER> barriers;
                barriers.reserve(uploads.size());

                for (auto& upload : uploads) {
                    for (auto& texture : upload.Textures) {
                        if (!texture || texture->OnHeap) continue;
                        barriers.push_back(texture->Barrier);
                    }
                }

                m_copyCommandList->ResourceBarrier((uint)barriers.size(), barriers.data());
            }

            // Create upload buffers for each texture and copy it over
            for (auto& upload : uploads) {
                Material2D material;
                material.Index = Render::Heaps->Shader.AllocateIndex();
                material.Name = upload.Name;

                // allocate a new heap range for the material
                for (int i = 0; i < 1/*Material2D::Count*/; i++) {
                    material.Handles[i] = Render::Heaps->Shader.GetGpuHandle(material.Index + i);
                    auto& uploadData = upload.Textures[i];
                    if (uploadData) {
                        //if (!uploadData->OnHeap) continue; // debug
                        if (uploadData->OnHeap) statsCommitted++;
                        else statsPlaced++;

                        CopyToGpu(*uploadData);
                        // take ownership
                        //material.Textures[i] = std::move(uploadData->Texture);
                    }

                    // Create views for each material, setting defaults (white/black/unknown)
                    auto handle = Render::Heaps->Shader.GetCpuHandle(material.Index + i);
                    auto tex2d = material.Textures[i] ? &material.Textures[i] : &_black;
                    tex2d->CreateShaderResourceView(handle);
                }
            }

            ThrowIfFailed(m_copyCommandList->Close());

            ID3D12CommandList* ppCommandLists[] = { m_copyCommandList.Get() };
            m_copyQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);


            WaitOnCopyQueue();
            _intermediates.clear();

            SPDLOG_INFO("LoadMaterials(): {:.3f}s Placed: {} Committed: {}", time.GetElapsedSeconds(), statsPlaced, statsCommitted);
            //Render::Adapter->PrintMemoryUsage();
            Render::Heaps->Shader.GetFreeDescriptors();
        }

    private:
        void LoadDefaults() {
            List<TextureUpload> uploads;

            List<uint8> black(64 * 64 * 4);
            FillTexture(black, 0, 0, 0, 255);

            List<uint8> white(64 * 64 * 4);
            FillTexture(white, 255, 255, 255, 255);

            List<uint8> purple(64 * 64 * 4);
            FillTexture(purple, 255, 0, 255, 255);

            uploads.push_back(LoadBitmapPlaced(black.data(), L"black", 64, 64));
            uploads.push_back(LoadBitmapPlaced(white.data(), L"white", 64, 64));
            uploads.push_back(LoadBitmapPlaced(purple.data(), L"purple", 64, 64));
            LoadTextures(uploads);

            _black = std::move(uploads[0].Texture);
            _white = std::move(uploads[1].Texture);
            _purple = std::move(uploads[2].Texture);


            //{
            //    _defaultMaterial.Name = "default";

            //    for (uint i = 0; i < Material2D::Count; i++) {
            //        // this makes the dangerous assumption that no other threads will allocate between iterations
            //        auto handle = Render::Heaps->Reserved.Allocate();
            //        _defaultMaterial.Handles[i] = handle.GetGpuHandle();

            //        if (i == 0)
            //            _purple.CreateShaderResourceView(handle.GetCpuHandle());
            //        else
            //            _black.CreateShaderResourceView(handle.GetCpuHandle());
            //    }

            //    for (uint i = 0; i < Material2D::Count; i++) {
            //        auto handle = Render::Heaps->Reserved.Allocate();
            //        //White.Handles[i] = handle.GetGpuHandle();

            //        if (i == 0)
            //            _white.CreateShaderResourceView(handle.GetCpuHandle());
            //        else
            //            _black.CreateShaderResourceView(handle.GetCpuHandle());
            //    }

            //    for (uint i = 0; i < Material2D::Count; i++) {
            //        auto handle = Render::Heaps->Reserved.Allocate();
            //        //Black.Handles[i] = handle.GetGpuHandle();

            //        _black.CreateShaderResourceView(handle.GetCpuHandle());
            //    }
            //}
        }

        Option<TextureUpload> LoadPigBitmap(TexID id, bool mask) {
            auto& bitmap = Resources::ReadBitmap(id);
            auto data = mask ? bitmap.Mask.data() : bitmap.Data.data();
            if (bitmap.Width * bitmap.Height * 4 <= 64 * 1024) // textures must be <= 64 kb to use placed allocation
                //return LoadBitmapCommitted(data, Convert::ToWideString(bitmap.Name), bitmap.Width, bitmap.Height);
                return LoadBitmapPlaced(data, Convert::ToWideString(bitmap.Name), bitmap.Width, bitmap.Height);
                //return { .OnHeap = false };
            else
                return {};
                //return { .OnHeap = false };
                //return LoadBitmapCommitted(data, Convert::ToWideString(bitmap.Name), bitmap.Width, bitmap.Height);
        }

        static TextureUpload LoadBitmapCommitted(const void* pData, wstring name, int width, int height, int bytesPerPixel = 4) {
            D3D12_SUBRESOURCE_DATA subresource = {};
            subresource.pData = pData;
            subresource.RowPitch = width * bytesPerPixel;
            subresource.SlicePitch = subresource.RowPitch * height;

            TextureUpload upload{};
            upload.subresources.push_back(subresource);
            upload.Texture.Create(width, height, name);
            upload.OnHeap = true;
            return upload;
        }

        // Loads raw bitmap data with no mipmaps
        TextureUpload LoadBitmapPlaced(const void* pData, wstring name, int width, int height, int bytesPerPixel = 4) {
            D3D12_SUBRESOURCE_DATA subresource = {};
            subresource.pData = pData;
            subresource.RowPitch = width * bytesPerPixel;
            subresource.SlicePitch = subresource.RowPitch * height;

            TextureUpload upload{};
            upload.subresources.push_back(subresource);
            upload.Texture.CreateNoHeap(width, height);

            auto [sizeInBytes, alignment] = upload.Texture.GetPlacementAlignment(_device);
            if (alignment + sizeInBytes > _heapSize)
                throw Exception("Out of memory in placed texture heap!");

            auto offset = (_allocated + (alignment - 1)) & ~(alignment - 1);
            upload.Barrier = upload.Texture.CreatePlacedResource(_device, _heap.Get(), offset, name);

            _allocated += alignment;
            return upload;
        }

        Option<TextureUpload> LoadDDS(const filesystem::path& path) const {
            TextureUpload upload;
            upload.Texture.LoadDDS(_device, path, upload.data, upload.subresources);
            upload.OnHeap = true;
            return upload;
        }

        void CopyToGpu(const TextureUpload& upload) {
            assert(!upload.subresources.empty() && upload.subresources[0].pData);

            const UINT64 intermediateSize =
                GetRequiredIntermediateSize(upload.Texture.Get(), 0, (uint)upload.subresources.size()) +
                D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

            auto heapType = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(intermediateSize);
            auto& intermediate = _intermediates.emplace_back();

            ThrowIfFailed(_device->CreateCommittedResource(
                &heapType,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&intermediate)));

            UpdateSubresources(m_copyCommandList.Get(), upload.Texture.Get(), intermediate.Get(), 0,
                               0, (uint)upload.subresources.size(), upload.subresources.data());
        }

        MaterialUpload LoadMaterial(TexID id, string name) {
            assert(id > TexID::Invalid);
            MaterialUpload material{};
            auto& ti = Resources::GetTextureInfo(id);
            material.Name = name.empty() ? material.Name : name;
            material.ID = id;

            bool loadedDiffuse = false;
            bool loadedST = false;

            // remove the frame number when loading special textures. They should be the same across all frames.
            string baseName = material.Name;
            if (auto i = baseName.find("#"); i > 0)
                baseName = baseName.substr(0, i);

            if (Settings::Graphics.HighRes) {
                if (auto path = FileSystem::TryFindFile(name + ".DDS")) {
                    material.Textures[Material2D::Diffuse] = LoadDDS(*path);
                }

                if (ti.SuperTransparent) {
                    if (auto path = FileSystem::TryFindFile(baseName + "_st.DDS"))
                        material.Textures[Material2D::SuperTransparency] = LoadDDS(*path);
                }
            }

            if (!loadedDiffuse) {
                material.Textures[Material2D::Diffuse] = LoadPigBitmap(id, false);
            }

            if (!loadedST && ti.SuperTransparent)
                material.Textures[Material2D::SuperTransparency] = LoadPigBitmap(id, true);

            if (auto path = FileSystem::TryFindFile(baseName + "_e.DDS"))
                material.Textures[Material2D::Emissive] = LoadDDS(*path);

            if (auto path = FileSystem::TryFindFile(baseName + "_s.DDS"))
                material.Textures[Material2D::Specular] = LoadDDS(*path);

            return material;
        }

        bool HasUnloadedTextures(span<const TexID> tids) const {
            bool hasPending = false;
            for (auto& id : tids) {
                if (id <= TexID::Invalid) continue;
                if (_materials[(int)id].ID == id) continue;
                hasPending = true;
                break;
            }

            return hasPending;
        }
    };

    //inline Ptr<MaterialLibrary2> Materials2;
}
