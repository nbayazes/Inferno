#include "pch.h"
#include "MaterialLibrary.h"
#include "Resources.h"
#include "Render.h"
#include "Game.h"
#include "Convert.h"

using namespace DirectX;

namespace Inferno::Render {
    ResourceUploadBatch BeginTextureUpload() {
        ResourceUploadBatch batch(Render::Device);
        batch.Begin();
        return batch;
    }

    ComPtr<ID3D12CommandQueue> EndTextureUpload(ResourceUploadBatch& batch) {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        ComPtr<ID3D12CommandQueue> cmdQueue;
        ThrowIfFailed(Render::Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue)));
        auto task = batch.End(cmdQueue.Get());
        task.wait();
        return cmdQueue; // unknown why we need to hold onto the queue, but it randomly crashes due to releasing too early
    }

    List<TexID> GetTexturesForModel(ModelID id) {
        List<TexID> ids;
        auto& model = Resources::GetModel(id);

        for (int16 i = 0; i < model.TextureCount; i++) {
            auto tid = Resources::LookupModelTexID(model, i);
            ids.push_back(tid);

            // Also load effect clip frames
            if (auto eclip = Resources::TryGetEffectClip(tid)) {
                for (auto& frame : eclip->VClip.GetFrames())
                    ids.push_back(frame);
            }
        }

        return ids;
    }

    Set<TexID> GetLevelModelTextures(const Inferno::Level& level) {
        Set<TexID> ids;

        // Textures for each object
        for (auto& object : level.Objects) {
            switch (object.Type) {
                case ObjectType::Robot:
                {
                    auto& info = Resources::GetRobotInfo(object.ID);
                    auto modelIds = GetTexturesForModel(info.Model);
                    ids.insert(modelIds.begin(), modelIds.end());
                    if (object.Render.Model.TextureOverride != LevelTexID::None) {
                        auto id = Resources::LookupLevelTexID(object.Render.Model.TextureOverride);
                        ids.insert(id);
                    }

                    break;
                }
                default:
                    if (object.Render.Type == RenderType::Polyobj) {
                        auto modelIds = GetTexturesForModel(object.Render.Model.ID);
                        ids.insert(modelIds.begin(), modelIds.end());
                    }
                    break;
            }
        }

        return ids;
    }

    Set<TexID> GetLevelSegmentTextures(const Inferno::Level& level) {
        Set<TexID> ids;

        for (auto& seg : level.Segments) {
            for (auto& sideId : SideIDs) {
                auto& side = seg.GetSide(sideId);
                if (!seg.SideHasConnection(sideId) || seg.SideIsWall(sideId)) {
                    ids.insert(Resources::LookupLevelTexID(side.TMap));

                    if (auto eclip = Resources::TryGetEffectClip(side.TMap)) {
                        for (auto& frame : eclip->VClip.GetFrames())
                            ids.insert(frame);
                    }
                }

                if (side.HasOverlay()) {
                    ids.insert(Resources::LookupLevelTexID(side.TMap2));
                    if (auto eclip = Resources::TryGetEffectClip(side.TMap2)) {
                        for (auto& frame : eclip->VClip.GetFrames())
                            ids.insert(frame);
                    }
                }
            }
        }

        return ids;
    }

    Set<TexID> GetVClipTextures(const Inferno::Level& level) {
        Set<TexID> vclips;

        for (auto& obj : level.Objects) {
            if (obj.Type == ObjectType::Powerup || obj.Type == ObjectType::Hostage) {
                auto& vclip = Resources::GetVideoClip(obj.Render.VClip.ID);
                Seq::insert(vclips, vclip.GetFrames());
            }
        }

        {
            auto& matcen = Resources::GetVideoClip(VClips::Matcen);
            Seq::insert(vclips, matcen.GetFrames()); // Always load matcen effect
        }
        return vclips;
    }

    // Gets the first frame of door textures for the wall clip dropdown
    List<TexID> GetDoorTextures() {
        List<TexID> ids;

        for (auto& clip : Resources::GameData.WallClips) {
            auto id = Resources::LookupLevelTexID(clip.Frames[0]);
            ids.push_back(id);
        }

        return ids;
    }

    Set<TexID> GetLevelTextures(const Level& level, bool preloadDoors) {
        if (!Resources::HasGameData()) return {};

        Set<TexID> ids;
        Seq::insert(ids, GetLevelSegmentTextures(level));
        Seq::insert(ids, GetLevelModelTextures(level));
        Seq::insert(ids, GetVClipTextures(level));
        if (preloadDoors)
            Seq::insert(ids, GetDoorTextures());

        // always keep texture 0 loaded
        auto defaultId = Resources::LookupLevelTexID(LevelTexID(0));
        ids.insert(defaultId);

        return ids;
    }

    Option<Material2D> UploadMaterial(ResourceUploadBatch& batch,
                                      MaterialUpload& upload,
                                      Texture2D& defaultTex) {
        if (upload.ID <= TexID::Invalid) return {};
        Material2D material;
        material.Index = Render::Heaps->Shader.AllocateIndex();

        // allocate a new heap range for the material
        for (int i = 0; i < Material2D::Count; i++)
            material.Handles[i] = Render::Heaps->Shader.GetGpuHandle(material.Index + i);

        material.Name = upload.Bitmap.Name;
        material.ID = upload.ID;

        bool loadedDiffuse = false;
        bool loadedST = false;

        // remove the frame number when loading special textures, as they should share.
        string baseName = material.Name;
        if (auto i = baseName.find("#"); i > 0)
            baseName = baseName.substr(0, i);

        //SPDLOG_INFO("Loading texture `{}` to heap index: {}", ti->Name, material.Index);
        if (Settings::HighRes) {
            if (auto path = FileSystem::TryFindFile(upload.Bitmap.Name + ".DDS"))
                loadedDiffuse = material.Textures[Material2D::Diffuse].LoadDDS(batch, *path);

            if (upload.SuperTransparent)
                if (auto path = FileSystem::TryFindFile(baseName + "_st.DDS"))
                    loadedST = material.Textures[Material2D::SuperTransparency].LoadDDS(batch, *path);
        }

        if (!loadedDiffuse) {
            if (!upload.Outrage.Data.empty())
                material.Textures[Material2D::Diffuse].Load(batch, upload.Outrage.Data.data(), upload.Outrage.Width, upload.Outrage.Height, Convert::ToWideString(upload.Outrage.Name));
            else
                material.Textures[Material2D::Diffuse].Load(batch, upload.Bitmap.Data.data(), upload.Bitmap.Width, upload.Bitmap.Height, Convert::ToWideString(upload.Bitmap.Name));
        }

        // todo: optimize by putting all materials into a dictionary or some other way of not reloading special maps
        if (!loadedST && upload.SuperTransparent)
            material.Textures[Material2D::SuperTransparency].Load(batch, upload.Bitmap.Mask.data(), upload.Bitmap.Width, upload.Bitmap.Height, Convert::ToWideString(upload.Bitmap.Name));

        if (auto path = FileSystem::TryFindFile(baseName + "_e.DDS"))
            material.Textures[Material2D::Emissive].LoadDDS(batch, *path);

        if (auto path = FileSystem::TryFindFile(baseName + "_s.DDS"))
            material.Textures[Material2D::Specular].LoadDDS(batch, *path);

        for (uint i = 0; i < std::size(material.Textures); i++) {
            auto handle = Render::Heaps->Shader.GetCpuHandle(material.Index + i);
            auto texture = material.Textures[i] ? &material.Textures[i] : &defaultTex;
            texture->CreateShaderResourceView(handle);
        }

        return { std::move(material) };
    }

    class MaterialUploadWorker : public WorkerThread {
        MaterialLibrary* _lib;
    public:
        MaterialUploadWorker(MaterialLibrary* lib) : _lib(lib) {}
    protected:
        void Work() override {
            auto batch = BeginTextureUpload();

            List<MaterialUpload> queuedUploads;
            _lib->RequestedUploads.ForEach([&queuedUploads](auto& x) {
                queuedUploads.push_back(std::move(x));
            });
            _lib->RequestedUploads.Clear();

            List<Material2D> uploads;
            for (auto& upload : queuedUploads) {
                if (upload.Bitmap.Width == 0 || upload.Bitmap.Height == 0)
                    continue;

                auto material = UploadMaterial(batch, upload, _lib->_black);
                if (material)
                    uploads.emplace_back(std::move(material.value()));
            }

            EndTextureUpload(batch);

            // update pointers as textures are now loaded
            for (auto& upload : uploads) {
                auto& existing = _lib->_materials[(int)upload.ID];
                for (size_t i = 0; i < 4; i++) {
                    existing.Handles[i] = upload.Handles[i];
                }

                _lib->PendingCopies.Add(std::move(upload)); // copies are performed on main thread
            }

            if (!uploads.empty()) {
                SPDLOG_INFO("Loaded {} textures on background thread", uploads.size());
                Render::Adapter->PrintMemoryUsage();
                Render::Heaps->Shader.GetFreeDescriptors();
            }

        }
    };


    MaterialLibrary::MaterialLibrary(size_t size)
        : _materials(size) {
        LoadDefaults();
        _worker = MakePtr<MaterialUploadWorker>(this);
        _worker->Start();
    }

    void MaterialLibrary::Shutdown() {
        _worker.reset();
    }

    void MaterialLibrary::LoadMaterials(span<const TexID> tids, bool forceLoad) {
        // Pre-scan materials, as starting an upload batch causes a stall
        if (!forceLoad && !HasUnloadedTextures(tids)) return;

        List<Material2D> uploads;
        auto batch = BeginTextureUpload();

        for (auto& id : tids) {
            auto upload = PrepareUpload(id, forceLoad);
            if (upload.Bitmap.Width == 0 || upload.Bitmap.Height == 0)
                continue;

            auto material = UploadMaterial(batch, upload, _black);
            if (material)
                uploads.emplace_back(std::move(material.value()));
        }

        SPDLOG_INFO("Loading {} textures", uploads.size());
        EndTextureUpload(batch);

        for (auto& upload : uploads)
            _materials[(int)upload.ID] = std::move(upload);

        Render::Adapter->PrintMemoryUsage();
        Render::Heaps->Shader.GetFreeDescriptors();
    }

    void MaterialLibrary::LoadMaterialsAsync(span<const TexID> tids, bool forceLoad) {
        if (!forceLoad && !HasUnloadedTextures(tids)) return;

        for (auto& id : tids)
            RequestedUploads.Add(PrepareUpload(id, forceLoad));

        _worker->Notify();
    }

    MaterialUpload MaterialLibrary::PrepareUpload(TexID id, bool forceLoad) {
        auto ti = Resources::TryGetTextureInfo(id);
        if (!ti) return {};
        if (!forceLoad && _materials[(int)id].ID == id) return {};

        MaterialUpload upload;
        upload.Bitmap = Resources::ReadBitmap(id);
        upload.ID = id;
        upload.SuperTransparent = ti->SuperTransparent;
        return upload;
    }

    void TrashTextures(List<Material2D>&& trash) {
        if (!trash.empty()) {
            SPDLOG_INFO("Trashing {} textures", trash.size());

            // Would be nice to do this on a different thread
            for (auto& item : trash)
                Render::Heaps->Shader.FreeIndex(item.Index);

            Render::Adapter->PrintMemoryUsage();
            Render::Heaps->Shader.GetFreeDescriptors();
        }
    }

    void MaterialLibrary::Dispatch() {
        Render::Adapter->WaitForGpu();

        if (!PendingCopies.IsEmpty()) {
            SPDLOG_INFO("Replacing visible textures");

            List<Material2D> trash;

            {
                PendingCopies.ForEach([this, &trash](Material2D& pending) {
                    int id = (int)pending.ID;
                    if (_materials[id].ID > TexID::Invalid)
                        trash.push_back(std::move(_materials[id])); // Dispose old texture if it was loaded

                    _materials[id] = std::move(pending);
                });

                PendingCopies.Clear();
            }

            if (!trash.empty()) {
                SPDLOG_INFO("Trashing {} textures", trash.size());

                // Would be nice to do this on a different thread
                for (auto& item : trash)
                    Render::Heaps->Shader.FreeIndex(item.Index);

                Render::Adapter->PrintMemoryUsage();
                Render::Heaps->Shader.GetFreeDescriptors();
            }
        }

        if (_requestPrune) PruneInternal();
    }

    void MaterialLibrary::LoadLevelTextures(const Inferno::Level& level, bool force) {
        SPDLOG_INFO("Load level textures. Force {}", force);
        Render::Adapter->WaitForGpu();
        KeepLoaded.clear();
        auto ids = GetLevelTextures(level, PreloadDoors);
        auto tids = Seq::ofSet(ids);
        LoadMaterials(tids, force);
    }

    void MaterialLibrary::Reload() {
        List<TexID> ids;

        _materials.ForEach([&ids](auto& material) {
            if (material.ID > TexID::Invalid)
                ids.push_back(material.ID);
        });

        LoadMaterialsAsync(ids, true);
        Prune();
    }

    void MaterialLibrary::PruneInternal() {
        auto ids = GetLevelTextures(Game::Level, PreloadDoors);
        Seq::insert(ids, KeepLoaded);

        List<Material2D> trash;

        _materials.ForEach([&trash, &ids](auto& material) {
            if (material.ID <= TexID::Invalid || ids.contains(material.ID)) return;
            trash.emplace_back(std::move(material));
            material = {}; // mark the material as unused
        });

        TrashTextures(std::move(trash));
        _requestPrune = false;
    }

    void MaterialLibrary::Unload() {
        SPDLOG_INFO("Unloading all textures");
        Render::Adapter->WaitForGpu();
        List<Material2D> trash;
        _materials.ForEach([&trash](auto& material) {
            if (material.ID <= TexID::Invalid) return;
            trash.emplace_back(std::move(material));
            material = {}; // mark the material as unused
        });

        TrashTextures(std::move(trash));
    }

    constexpr void FillTexture(span<ubyte> data, ubyte red, ubyte green, ubyte blue, ubyte alpha) {
        for (size_t i = 0; i < data.size() / 4; i++) {
            data[i * 4] = red;
            data[i * 4 + 1] = green;
            data[i * 4 + 2] = blue;
            data[i * 4 + 3] = alpha;
        }
    }

    void MaterialLibrary::LoadDefaults() {
        auto batch = BeginTextureUpload();

        List<ubyte> bmp(64 * 64 * 4);
        FillTexture(bmp, 0, 0, 0, 255);
        _black.Load(batch, bmp.data(), 64, 64, L"black");

        FillTexture(bmp, 255, 255, 255, 255);
        _white.Load(batch, bmp.data(), 64, 64, L"white");

        FillTexture(bmp, 255, 0, 255, 255);
        _purple.Load(batch, bmp.data(), 64, 64, L"purple");

        {
            _defaultMaterial.Name = "default";

            for (uint i = 0; i < Material2D::Count; i++) {
                // this makes the dangerous assumption that no other threads will allocate between iterations
                auto handle = Render::Heaps->Reserved.Allocate();
                _defaultMaterial.Handles[i] = handle.GetGpuHandle();

                if (i == 0)
                    _purple.CreateShaderResourceView(handle.GetCpuHandle());
                else
                    _black.CreateShaderResourceView(handle.GetCpuHandle());
            }

            for (uint i = 0; i < Material2D::Count; i++) {
                auto handle = Render::Heaps->Reserved.Allocate();
                White.Handles[i] = handle.GetGpuHandle();

                if (i == 0)
                    _white.CreateShaderResourceView(handle.GetCpuHandle());
                else
                    _black.CreateShaderResourceView(handle.GetCpuHandle());
            }
        }


        EndTextureUpload(batch);
    }

    // The following is an attempt at localizing ResourceUploadBatch() for debugging

    //std::mutex TrackedMutex;
    //List<ComPtr<ID3D12Resource>> mTrackedObjects;
    //// Asynchronously uploads a resource. The memory in subRes is copied.
    //// The resource must be in the COPY_DEST state.
    //void Upload(_In_ ID3D12Resource* dest,
    //            uint32_t subresourceIndexStart,
    //            _In_reads_(numSubresources) const D3D12_SUBRESOURCE_DATA* subRes,
    //            uint32_t numSubresources,
    //            ID3D12GraphicsCommandList* cmdList) {

    //    UINT64 uploadSize = GetRequiredIntermediateSize(
    //        dest,
    //        subresourceIndexStart,
    //        numSubresources);

    //    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
    //    CD3DX12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);

    //    // Create a temporary buffer

    //    std::scoped_lock lock(TrackedMutex);
    //    auto& scratchResource = mTrackedObjects.emplace_back();

    //    ThrowIfFailed(Render::Device->CreateCommittedResource(
    //        &heapProps,
    //        D3D12_HEAP_FLAG_NONE,
    //        &resDesc,
    //        D3D12_RESOURCE_STATE_GENERIC_READ,
    //        nullptr, // D3D12_CLEAR_VALUE* pOptimizedClearValue
    //        IID_GRAPHICS_PPV_ARGS(scratchResource.ReleaseAndGetAddressOf())));

    //    SetDebugObjectName(scratchResource.Get(), L"ResourceUploadBatch Temporary");

    //    // Submit resource copy to command list
    //    UpdateSubresources(cmdList, dest, scratchResource.Get(), 0,
    //                       subresourceIndexStart, numSubresources, subRes);
    //    // Remember this upload object for delayed release
    //    //mTrackedObjects.emplace_back(scratchResource);
    //    //return std::move(scratchResource);
    //}

    //Option<Material2D> MaterialLibrary::LoadMaterial2(ID3D12GraphicsCommandList* cmdList, TexID id) {
    //    auto ti = Resources::TryGetTextureInfo(id);
    //    if (!ti) return {};

    //    Material2D material;
    //    material.Index = Render::Heaps->Shader.AllocateIndex();

    //    // allocate a new heap range for the material
    //    for (int i = 0; i < Material2D::Count; i++)
    //        material.Handles[i] = Render::Heaps->Shader.GetGpuHandle(material.Index + i);

    //    auto bmp = Resources::ReadBitmap(id);
    //    material.Name = ti->Name;
    //    material.ID = id;

    //    auto LoadDDS = [cmdList](Texture2D& texture, wstring file) {
    //        if (auto path = FileSystem::TryFindFile(file)) {
    //            auto [data, subres] = texture.CreateFromDDS(*path);
    //            Upload(texture.Get(), 0, subres.data(), (uint)subres.size(), cmdList);
    //            texture.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    //            return true;
    //        }
    //        return false;
    //    };

    //    auto LoadPigBitmap = [cmdList](Texture2D& texture, grs_bitmap& bmp) {
    //        texture.Create(bmp.Width, bmp.Height, Convert::ToWideString(bmp.Name));
    //        texture.Transition(cmdList, D3D12_RESOURCE_STATE_COPY_DEST);

    //        D3D12_SUBRESOURCE_DATA subres = {};
    //        subres.pData = bmp.Data.data();
    //        subres.RowPitch = bmp.Width * 4;
    //        subres.SlicePitch = bmp.Width * 4 * bmp.Height;

    //        Upload(texture.Get(), 0, &subres, 1, cmdList);
    //        texture.Transition(cmdList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    //    };

    //    //SPDLOG_INFO("Loading texture `{}` to heap index: {}", ti->Name, material.Index);

    //    if (!Render::HighRes || !LoadDDS(material.Textures[Material2D::Diffuse], Convert::ToWideString(ti->Name + ".DDS"))) {
    //        //material.Textures[Material2D::Diffuse].Load(batch, bmp.Data.data(), ti->Width, ti->Height, Convert::ToWideString(ti->Name));
    //        LoadPigBitmap(material.Textures[Material2D::Diffuse], bmp);
    //    }

    //    // remove the frame number when loading special textures, as they should share.
    //    string name = material.Name;
    //    if (auto i = name.find("#"); i > 0)
    //        name = name.substr(0, i);

    //    // todo: optimize by putting all materials into a dictionary or some other way of not reloading special maps
    //    if (ti->SuperTransparent) {
    //        //if (!Render::HighRes || !material.Textures[Material2D::SuperTransparency].LoadDDS(batch, name + "_st.DDS"))
    //        if (!Render::HighRes || !LoadDDS(material.Textures[Material2D::SuperTransparency], Convert::ToWideString(name + "_st.DDS")))
    //            //material.Textures[Material2D::SuperTransparency].Load(batch, bmp.Mask.data(), ti->Width, ti->Height, ti->Name);
    //            LoadPigBitmap(material.Textures[Material2D::SuperTransparency], bmp);
    //    }

    //    LoadDDS(material.Textures[Material2D::Emissive], Convert::ToWideString(ti->Name + "_e.DDS"));
    //    LoadDDS(material.Textures[Material2D::Specular], Convert::ToWideString(ti->Name + "_s.DDS"));

    //    for (uint i = 0; i < std::size(material.Textures); i++) {
    //        auto handle = Render::Heaps->Shader.GetCpuHandle(material.Index + i);
    //        auto texture = material.Textures[i] ? &material.Textures[i] : &_black; // default to black if no texture present
    //        texture->CreateShaderResourceView(handle);
    //    }

    //    return { std::move(material) };
    //}

    //Tuple<ComPtr<ID3D12CommandAllocator>, ComPtr<ID3D12GraphicsCommandList>> BeginUpload() {
    //    D3D12_COMMAND_LIST_TYPE commandType = D3D12_COMMAND_LIST_TYPE_DIRECT;
    //    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    //    ComPtr<ID3D12GraphicsCommandList> cmdList;
    //    ThrowIfFailed(Render::Device->CreateCommandAllocator(commandType, IID_GRAPHICS_PPV_ARGS(cmdAlloc.ReleaseAndGetAddressOf())));
    //    SetDebugObjectName(cmdAlloc.Get(), L"ResourceUploadBatch");

    //    ThrowIfFailed(Render::Device->CreateCommandList(1, commandType, cmdAlloc.Get(), nullptr, IID_GRAPHICS_PPV_ARGS(cmdList.ReleaseAndGetAddressOf())));
    //    SetDebugObjectName(cmdList.Get(), L"ResourceUploadBatch");

    //    return { cmdAlloc, cmdList };
    //}

    ////List<ComPtr<ID3D12Resource>> mTrackedObjects;
    ////List<ComPtr<ID3D12Resource>>& trackedObjects
    //std::future<void> EndUpload(_In_ ID3D12CommandQueue* commandQueue, ID3D12GraphicsCommandList* cmdList) {
    //    ThrowIfFailed(cmdList->Close());

    //    // Submit the job to the GPU
    //    commandQueue->ExecuteCommandLists(1, CommandListCast(&cmdList));

    //    // Set an event so we get notified when the GPU has completed all its work
    //    ComPtr<ID3D12Fence> fence;
    //    ThrowIfFailed(Render::Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_GRAPHICS_PPV_ARGS(fence.GetAddressOf())));

    //    SetDebugObjectName(fence.Get(), L"ResourceUploadBatch");

    //    HANDLE gpuCompletedEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_MODIFY_STATE | SYNCHRONIZE);
    //    if (!gpuCompletedEvent)
    //        throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()), "CreateEventEx");

    //    ThrowIfFailed(commandQueue->Signal(fence.Get(), 1ULL));
    //    ThrowIfFailed(fence->SetEventOnCompletion(1ULL, gpuCompletedEvent));

    //    struct UploadBatch {
    //        //std::vector<ComPtr<ID3D12DeviceChild>>  TrackedObjects;
    //        //std::vector<SharedGraphicsResource>     TrackedMemoryResources;
    //        ComPtr<ID3D12GraphicsCommandList>       CommandList;
    //        ComPtr<ID3D12Fence>                     Fence;
    //        ScopedHandle                            GpuCompleteEvent;
    //    };

    //    // Create a packet of data that'll be passed to our waiting upload thread
    //    UploadBatch uploadBatch;
    //    uploadBatch.CommandList = cmdList;
    //    uploadBatch.Fence = fence;
    //    uploadBatch.GpuCompleteEvent.reset(gpuCompletedEvent);
    //    //std::swap(trackedObjects, uploadBatch->TrackedObjects);
    //    //std::swap(mTrackedMemoryResources, uploadBatch->TrackedMemoryResources);

    //    // Kick off a thread that waits for the upload to complete on the GPU timeline.
    //    // Let the thread run autonomously, but provide a future the user can wait on.
    //    std::future<void> future = std::async(std::launch::async, [uploadBatch = std::move(uploadBatch)]() {
    //        // Wait on the GPU-complete notification
    //        DWORD wr = WaitForSingleObject(uploadBatch.GpuCompleteEvent.get(), INFINITE);
    //        if (wr != WAIT_OBJECT_0) {
    //            if (wr == WAIT_FAILED) {
    //                throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()), "WaitForSingleObject");
    //            }
    //            else {
    //                throw std::runtime_error("WaitForSingleObject");
    //            }
    //        }
    //    });

    //    return future;
    //}


    //std::future<void>& MaterialLibrary::LoadMaterialsAsync2(span<TexID> ids, bool forceLoad) {
    //    List<TexID> tids(ids.begin(), ids.end());

    //    //UploadHandle = std::async(std::launch::async, &MaterialLibrary::LoadMaterials2, this, ids, forceLoad);
    //    StartAsync([this, forceLoad, tids] {
    //        auto [cmdAlloc, cmdList] = BeginUpload();

    //        //UploadHandle = std::async(std::launch::async, [this, forceLoad, tids] {
    //        List<Material2D> uploads;

    //        {
    //            std::scoped_lock lock(UploadCompleteMutex);
    //            UploadsInProgress++;
    //        }
    //        SPDLOG_INFO("LoadMaterialsAsync2() - In Progress: {}", UploadsInProgress);

    //        List<TexID> pending;
    //        std::ranges::transform(PendingCopies, std::back_inserter(pending), [](auto& c) { return c.ID; });
    //        for (auto& id : tids) {
    //            if (id <= TexID::Invalid) continue;
    //            if (!forceLoad && _materials[(int)id].ID == id) continue;
    //            if (Seq::exists(pending, id)) continue;
    //            auto material = LoadMaterial2(cmdList.Get(), id);
    //            if (material)
    //                uploads.emplace_back(std::move(material.value()));
    //        }

    //        auto cmdQueue = CreateCommandQueue();
    //        EndUpload(cmdQueue.Get(), cmdList.Get());

    //        SPDLOG_INFO("Loading {} textures on background thread", uploads.size());

    //        // update pointers as textures are now loaded
    //        std::scoped_lock lock(PendingCopiesMutex);
    //        for (auto& upload : uploads) {
    //            assert(upload.ID > TexID::Invalid);
    //            auto& existing = _materials[(int)upload.ID]; // materials could have changed from another load command
    //            for (size_t i = 0; i < 4; i++) {
    //                std::scoped_lock mtlLock(MaterialMutex);
    //                existing.Handles[i] = upload.Handles[i];
    //            }

    //            PendingCopies.push_back(std::move(upload));
    //        }

    //        {
    //            std::scoped_lock lock(UploadCompleteMutex);
    //            UploadsInProgress--;
    //            if (UploadsInProgress == 0) UploadComplete.notify_all();
    //        }
    //        SPDLOG_INFO("Load complete - In progress: {}", UploadsInProgress);

    //        Render::Adapter->PrintMemoryUsage();
    //        Render::Heaps->Shader.GetFreeDescriptors();
    //    });

    //    // todo: create a future that returns false until pending completes
    //    return UploadHandle;
    //}

}
