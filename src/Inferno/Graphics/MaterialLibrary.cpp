#include "pch.h"
#include "MaterialLibrary.h"
#include "Resources.h"
#include "Render.h"
#include "Game.h"
#include "Convert.h"
#include "FileSystem.h"
#include "ScopedTimer.h"
#include "NormalMap.h"

using namespace DirectX;

namespace Inferno::Render {
    constexpr void FillTexture(span<ubyte> data, ubyte red, ubyte green, ubyte blue, ubyte alpha) {
        for (size_t i = 0; i < data.size() / 4; i++) {
            data[i * 4] = red;
            data[i * 4 + 1] = green;
            data[i * 4 + 2] = blue;
            data[i * 4 + 3] = alpha;
        }
    }

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
            auto& eclip = Resources::GetEffectClip(tid);
            Seq::append(ids, eclip.VClip.GetFrames());
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
                        auto id = Resources::LookupTexID(object.Render.Model.TextureOverride);
                        ids.insert(id);
                    }

                    break;
                }
                default:
                    if (object.Render.Type == RenderType::Model) {
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
                    ids.insert(Resources::LookupTexID(side.TMap));
                    auto& eclip = Resources::GetEffectClip(side.TMap);
                    Seq::insert(ids, eclip.VClip.GetFrames());
                }

                if (side.HasOverlay()) {
                    ids.insert(Resources::LookupTexID(side.TMap2));
                    auto& eclip = Resources::GetEffectClip(side.TMap2);
                    Seq::insert(ids, eclip.VClip.GetFrames());

                    auto& destroyed = Resources::GetVideoClip(eclip.DestroyedVClip);
                    Seq::insert(ids, destroyed.GetFrames());

                    ids.insert(Resources::LookupTexID(eclip.DestroyedTexture));
                }

                // Door clips
                if (auto wall = level.TryGetWall(side.Wall)) {
                    auto& wclip = Resources::GetDoorClip(wall->Clip);
                    auto wids = Seq::map(wclip.GetFrames(), Resources::LookupTexID);
                    Seq::insert(ids, wids);
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
            auto& matcen = Resources::GetVideoClip(VClipID::Matcen);
            Seq::insert(vclips, matcen.GetFrames()); // Always load matcen effect
        }
        return vclips;
    }

    // Gets the first frame of door textures for the wall clip dropdown
    List<TexID> GetDoorTextures() {
        List<TexID> ids;

        for (auto& clip : Resources::GameData.DoorClips) {
            auto id = Resources::LookupTexID(clip.Frames[0]);
            ids.push_back(id);
        }

        return ids;
    }

    Set<TexID> GetAllVClips() {
        Set<TexID> ids;

        for (auto& vclip : Resources::GameData.VClips) {
            Seq::insert(ids, vclip.GetFrames());
        }

        return ids;
    }

    List<TexID> GetUITextures() {
        List<TexID> ids;

        for (auto& gauges : Resources::GameData.HiResGauges) {
            ids.push_back(gauges);
        }

        // menu background?

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
        auto defaultId = Resources::LookupTexID(LevelTexID(0));
        ids.insert(defaultId);

        return ids;
    }

    // Expands a diffuse texture by 1 pixel. Fixes artifacts around transparent edges.
    void ExpandDiffuse(const PigEntry& bmp, List<Palette::Color>& data) {
        auto getPixel = [&](int x, int y) -> Palette::Color& {
            if (x < 0) x += bmp.Width;
            if (x > bmp.Width - 1) x -= bmp.Width;
            if (y < 0) y += bmp.Height;
            if (y > bmp.Height - 1) y -= bmp.Height;

            int offset = bmp.Width * y + x;
            return data[offset];
        };

        auto spreadPixel = [](const Palette::Color& src, Palette::Color& dst) {
            dst.r = src.r;
            dst.g = src.g;
            dst.b = src.b;
        };

        // row pass. starts at top left.
        for (int y = 0; y < bmp.Height; y++) {
            for (int x = 0; x < bmp.Width; x++) {
                auto& px = data[bmp.Width * y + x];
                auto& below = getPixel(x, y + 1);
                auto& above = getPixel(x, y - 1);
                // row below is transparent and this one isn't
                if (below.a == 0 && px.a != 0) spreadPixel(px, below);

                // row above is transparent and this one isn't
                if (above.a == 0 && px.a != 0) spreadPixel(px, above);
            }
        }

        // column pass. starts at top left.
        for (int x = 0; x < bmp.Width; x++) {
            for (int y = 0; y < bmp.Height; y++) {
                auto& px = data[bmp.Width * y + x];
                auto& left = getPixel(x - 1, y);
                auto& right = getPixel(x + 1, y);
                // column left is transparent and this one isn't
                if (left.a == 0 && px.a != 0) spreadPixel(px, left);

                // column right is transparent and this one isn't
                if (right.a == 0 && px.a != 0) spreadPixel(px, right);
            }
        }
    }

    // Expands a supertransparent mask by 1 pixel. Fixes artifacts around supertransparent pixels.
    void ExpandMask(const PigEntry& bmp, List<Palette::Color>& data) {
        auto getPixel = [&](int x, int y) -> Palette::Color& {
            if (x < 0) x += bmp.Width;
            if (x > bmp.Width - 1) x -= bmp.Width;
            if (y < 0) y += bmp.Height;
            if (y > bmp.Height - 1) y -= bmp.Height;

            int offset = bmp.Width * y + x;
            return data[offset];
        };

        auto markMask = [](Palette::Color& dst) {
            dst.r = 128;
            dst.g = 0;
            dst.b = 0;
        };

        // row pass. starts at top left.
        for (int y = 0; y < bmp.Height; y++) {
            for (int x = 0; x < bmp.Width; x++) {
                auto& px = data[bmp.Width * y + x];
                auto& below = getPixel(x, y + 1);
                auto& above = getPixel(x, y - 1);
                // row below is masked and this one isn't
                if (below.r == 255 && px.r != 255) markMask(px);

                // row above is masked and this one isn't
                if (above.r == 255 && px.r != 255) markMask(px);
            }
        }

        // column pass. starts at top left.
        for (int x = 0; x < bmp.Width; x++) {
            for (int y = 0; y < bmp.Height; y++) {
                auto& px = data[bmp.Width * y + x];
                auto& left = getPixel(x - 1, y);
                auto& right = getPixel(x + 1, y);
                // column left is masked and this one isn't
                if (left.r == 255 && px.r != 255) markMask(px);

                // column right is masked and this one isn't
                if (right.r == 255 && px.r != 255) markMask(px);
            }
        }

        // Update the marked pixels to 255
        for (auto& px : data) {
            if (px.r > 0) px.r = 255;
        }
    }

    Option<Material2D> UploadMaterial(ResourceUploadBatch& batch,
                                      MaterialUpload& upload,
                                      Texture2D& blackTex,
                                      Texture2D& whiteTex,
                                      Texture2D& normalTex) {
        if (upload.ID <= TexID::Invalid) return {};
        Material2D material;
        material.Index = Render::Heaps->Shader.AllocateIndex();

        // allocate a new heap range for the material
        for (int i = 0; i < Material2D::Count; i++)
            material.Handles[i] = Render::Heaps->Shader.GetGpuHandle(material.Index + i);

        material.Name = upload.Bitmap->Info.Name;
        material.ID = upload.ID;

        // remove the frame number when loading special textures, as they should share.
        string baseName = material.Name;
        if (auto i = baseName.find("#"); i > 0)
            baseName = baseName.substr(0, i);

        const auto width = upload.Bitmap->Info.Width;
        const auto height = upload.Bitmap->Info.Height;

        //SPDLOG_INFO("Loading texture `{}` to heap index: {}", ti->Name, material.Index);
        if (Settings::Graphics.HighRes) {
            if (auto path = FileSystem::TryFindFile(material.Name + ".DDS"))
                material.Textures[Material2D::Diffuse].LoadDDS(batch, *path, true);

            if (upload.SuperTransparent)
                if (auto path = FileSystem::TryFindFile(baseName + "_st.DDS"))
                    material.Textures[Material2D::SuperTransparency].LoadDDS(batch, *path);
        }

        if (!material.Textures[Material2D::Diffuse]) {
            if (upload.Bitmap->Info.Transparent) {
                List<Palette::Color> data = upload.Bitmap->Data; // copy mask, as modifying the original would affect collision
                ExpandDiffuse(upload.Bitmap->Info, data);
                material.Textures[Material2D::Diffuse].Load(batch, data.data(), width, height, Convert::ToWideString(material.Name));
            } else {
                material.Textures[Material2D::Diffuse].Load(batch, upload.Bitmap->Data.data(), width, height, Convert::ToWideString(material.Name));
            }
        }

        if (!material.Textures[Material2D::SuperTransparency] && upload.SuperTransparent) {
            List<Palette::Color> mask = upload.Bitmap->Mask; // copy mask, as modifying the original would affect collision
            ExpandMask(upload.Bitmap->Info, mask);
            material.Textures[Material2D::SuperTransparency].Load(batch, mask.data(), width, height, Convert::ToWideString(material.Name), true, DXGI_FORMAT_R8G8B8A8_UNORM);
        }

        if (auto path = FileSystem::TryFindFile(baseName + "_e.DDS"))
            material.Textures[Material2D::Emissive].LoadDDS(batch, *path);

        if (auto path = FileSystem::TryFindFile(baseName + "_s.DDS"))
            material.Textures[Material2D::Specular].LoadDDS(batch, *path);

        auto& info = Resources::GetTextureInfo(material.ID);

        //if (!material.Textures[Material2D::Specular] && Resources::IsLevelTexture(upload.ID)) {
        if (!material.Textures[Material2D::Specular] && info.Width == 64 && info.Height == 64 && Settings::Inferno.GenerateMaps) {
            auto specular = CreateSpecularMap(*upload.Bitmap);
            material.Textures[Material2D::Specular].Load(batch, specular.data(), width, height, Convert::ToWideString(material.Name), true, DXGI_FORMAT_R8G8B8A8_UNORM);
        }

        if (!material.Textures[Material2D::Normal] && info.Width == 64 && info.Height == 64 && Settings::Inferno.GenerateMaps) {
            auto normal = CreateNormalMap(*upload.Bitmap);
            material.Textures[Material2D::Normal].Load(batch, normal.data(), width, height, Convert::ToWideString(material.Name), true, DXGI_FORMAT_R8G8B8A8_UNORM);
        }

        for (uint i = 0; i < std::size(material.Textures); i++) {
            auto handle = Render::Heaps->Shader.GetCpuHandle(material.Index + i);
            Texture2D* texture;
            if (material.Textures[i]) {
                texture = &material.Textures[i];
            }
            else {
                if (i == Material2D::Normal)
                    texture = &normalTex;
                else
                    texture = info.Transparent && i == Material2D::Specular ? &whiteTex : &blackTex;
            }

            texture->CreateShaderResourceView(handle);
        }

        return material;
    }

    Option<Material2D> UploadBitmap(ResourceUploadBatch& batch, string name, const Texture2D& defaultTex) {
        Material2D material;
        material.Index = Render::Heaps->Shader.AllocateIndex();

        // allocate a new heap range for the material
        for (int i = 0; i < Material2D::Count; i++)
            material.Handles[i] = Render::Heaps->Shader.GetGpuHandle(material.Index + i);

        material.Name = name;
        if (auto path = FileSystem::TryFindFile(name + ".DDS"))
            material.Textures[Material2D::Diffuse].LoadDDS(batch, *path, true);

        // Set default secondary textures
        for (uint i = 0; i < std::size(material.Textures); i++) {
            auto handle = Render::Heaps->Shader.GetCpuHandle(material.Index + i);
            auto texture = material.Textures[i] ? &material.Textures[i] : &defaultTex;
            texture->CreateShaderResourceView(handle);
        }

        return material;
    }

    Option<Material2D> UploadOutrageMaterial(ResourceUploadBatch& batch,
                                             const Outrage::Bitmap& bitmap,
                                             const Texture2D& defaultTex) {
        Material2D material;
        material.Index = Render::Heaps->Shader.AllocateIndex();
        assert(!bitmap.Mips.empty());

        // allocate a new heap range for the material
        for (int i = 0; i < Material2D::Count; i++)
            material.Handles[i] = Render::Heaps->Shader.GetGpuHandle(material.Index + i);

        material.Name = bitmap.Name;
        material.Textures[Material2D::Diffuse].Load(batch, bitmap.Mips[0].data(), bitmap.Width, bitmap.Height, Convert::ToWideString(bitmap.Name));

        // Set default secondary textures
        for (uint i = 0; i < std::size(material.Textures); i++) {
            auto handle = Render::Heaps->Shader.GetCpuHandle(material.Index + i);
            auto texture = material.Textures[i] ? &material.Textures[i] : &defaultTex;
            texture->CreateShaderResourceView(handle);
        }

        return material;
    }

    class MaterialUploadWorker : public WorkerThread {
        MaterialLibrary* _lib;

    public:
        MaterialUploadWorker(MaterialLibrary* lib) : _lib(lib) {}

    protected:
        void Work() override {
            auto batch = BeginTextureUpload();

            List<MaterialUpload> queuedUploads;
            _lib->_requestedUploads.ForEach([&queuedUploads](auto& x) {
                queuedUploads.push_back(std::move(x));
            });
            _lib->_requestedUploads.Clear();

            List<Material2D> uploads;
            for (auto& upload : queuedUploads) {
                if (!upload.Bitmap || upload.Bitmap->Info.Width == 0 || upload.Bitmap->Info.Height == 0)
                    continue;

                if (auto material = UploadMaterial(batch, upload, _lib->_black, _lib->_white, _lib->_normal))
                    uploads.emplace_back(std::move(material.value()));
            }

            EndTextureUpload(batch);

            // update pointers as textures are now loaded
            for (auto& upload : uploads) {
                auto& existing = _lib->_materials[(int)upload.ID];
                for (size_t i = 0; i < 4; i++) {
                    existing.Handles[i] = upload.Handles[i];
                }

                _lib->_pendingCopies.Add(std::move(upload)); // copies are performed on main thread
            }

            if (!uploads.empty()) {
                SPDLOG_INFO("Loaded {} textures on background thread", uploads.size());
                Render::Adapter->PrintMemoryUsage();
                Render::Heaps->Shader.GetFreeDescriptors();
            }
        }
    };


    MaterialLibrary::MaterialLibrary(size_t size)
        : _materials(size), _materialInfo(size) {
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

        Stopwatch time;
        List<Material2D> uploads;
        auto batch = BeginTextureUpload();

        for (auto& id : tids) {
            if (id == TexID::None) continue;
            auto upload = PrepareUpload(id, forceLoad);
            if (!upload.Bitmap || upload.Bitmap->Info.Width == 0 || upload.Bitmap->Info.Height == 0)
                continue;

            if (auto material = UploadMaterial(batch, upload, _black, _white, _normal))
                uploads.emplace_back(std::move(material.value()));
        }

        SPDLOG_INFO("Loading {} textures", uploads.size());
        EndTextureUpload(batch);

        for (auto& upload : uploads)
            _materials[(int)upload.ID] = std::move(upload);

        SPDLOG_INFO("LoadMaterials: {:.3f}s", time.GetElapsedSeconds());
        Render::Adapter->PrintMemoryUsage();
        Render::Heaps->Shader.GetFreeDescriptors();
    }

    void MaterialLibrary::LoadMaterialsAsync(span<const TexID> ids, bool forceLoad) {
        if (!forceLoad && !HasUnloadedTextures(ids)) return;

        for (auto& id : ids) {
            if (_submittedUploads.contains(id)) continue;
            _requestedUploads.Add(PrepareUpload(id, forceLoad));
            _submittedUploads.insert(id);
        }

        _worker->Notify();
    }

    MaterialUpload MaterialLibrary::PrepareUpload(TexID id, bool forceLoad) {
        if (!forceLoad && _materials[(int)id].ID == id) return {};

        MaterialUpload upload;
        upload.Bitmap = &Resources::GetBitmap(id);
        upload.ID = id;
        upload.SuperTransparent = Resources::GetTextureInfo(id).SuperTransparent;
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

        if (!_pendingCopies.IsEmpty()) {
            SPDLOG_INFO("Replacing visible textures");

            List<Material2D> trash;

            {
                _pendingCopies.ForEach([this, &trash](Material2D& pending) {
                    int id = (int)pending.ID;
                    if (_materials[id].ID > TexID::Invalid)
                        trash.push_back(std::move(_materials[id])); // Dispose old texture if it was loaded

                    _materials[id] = std::move(pending);
                });

                _submittedUploads.clear();
                _pendingCopies.Clear();
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

    void MaterialLibrary::LoadTextures(span<string> names) {
        bool hasUnloaded = false;
        for (auto& name : names) {
            if (!name.empty() && !_unpackedMaterials.contains(name)) {
                hasUnloaded = true;
                break;
            }
        }

        if (!hasUnloaded) return;
        Render::Adapter->WaitForGpu();

        List<Material2D> uploads;
        auto batch = BeginTextureUpload();

        for (auto& name : names) {
            if (_unpackedMaterials.contains(name)) continue; // skip loaded
            bool found = false;

            if (FileSystem::TryFindFile(name + ".dds")) {
                if (auto material = UploadBitmap(batch, name, _black)) {
                    uploads.emplace_back(std::move(material.value()));
                    found = true;
                }
            }
            else {
                // Try loading named file from D3 data
                if (auto bitmap = Resources::ReadOutrageBitmap(name + ".ogf")) {
                    if (auto material = UploadOutrageMaterial(batch, *bitmap, _black)) {
                        material->Name = name;
                        uploads.emplace_back(std::move(material.value()));
                        found = true;
                    }
                }
            }

            // Add entries that aren't found so it skipped in future loads
            if (!found) _unpackedMaterials[name] = {};
        }

        EndTextureUpload(batch);

        for (auto& upload : uploads)
            _unpackedMaterials[upload.Name] = std::move(upload);
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

    void MaterialLibrary::LoadDefaults() {
        auto batch = BeginTextureUpload();

        List<ubyte> bmp(64 * 64 * 4);
        FillTexture(bmp, 0, 0, 0, 255);
        _black.Load(batch, bmp.data(), 64, 64, L"black", false);

        FillTexture(bmp, 255, 255, 255, 255);
        _white.Load(batch, bmp.data(), 64, 64, L"white", false);

        FillTexture(bmp, 255, 0, 255, 255);
        _purple.Load(batch, bmp.data(), 64, 64, L"purple", false);

        FillTexture(bmp, 128, 128, 255, 0);
        _normal.Load(batch, bmp.data(), 64, 64, L"normal", false, DXGI_FORMAT_R8G8B8A8_UNORM);

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

            for (uint i = 0; i < Material2D::Count; i++) {
                auto handle = Render::Heaps->Reserved.Allocate();
                Black.Handles[i] = handle.GetGpuHandle();

                _black.CreateShaderResourceView(handle.GetCpuHandle());
            }
        }

        EndTextureUpload(batch);
    }
}
