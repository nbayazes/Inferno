#include "pch.h"
#include "MaterialLibrary.h"
#include "Convert.h"
#include "FileSystem.h"
#include "Game.h"
#include "Render.h"
#include "Resources.h"
#include "ScopedTimer.h"

using namespace DirectX;

namespace Inferno::Render {
    namespace {
        std::mutex WorkerMutex;
    }

    constexpr void FillTexture(span<ubyte> data, ubyte red, ubyte green, ubyte blue, ubyte alpha) {
        for (size_t i = 0; i < data.size() / 4; i++) {
            data[i * 4] = red;
            data[i * 4 + 1] = green;
            data[i * 4 + 2] = blue;
            data[i * 4 + 3] = alpha;
        }
    }

    void MoveUploads(span<Material2D> uploads, span<Material2D> materials) {
        Set<int> uploadIndices;

        for (auto& upload : uploads) {
            assert(!uploadIndices.contains(upload.UploadIndex));
            uploadIndices.insert(upload.UploadIndex);

            // Copy descriptors from upload to shader visible heap
            auto src = Render::Uploads->GetCpuHandle(upload.UploadIndex);
            auto dest = Render::Heaps->Materials.GetCpuHandle((int)upload.ID * 5);
            Render::Device->CopyDescriptorsSimple(Material2D::Count, dest, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            // Update the upload to use the new handles
            for (int i = 0; i < Material2D::Count; i++)
                upload.Handles[i] = Render::Heaps->Materials.GetGpuHandle((int)upload.ID * 5 + i);

            Render::Uploads->FreeIndex(upload.UploadIndex);
            upload.State = TextureState::Resident;
            materials[(int)upload.ID] = std::move(upload);
        }
    }

    ResourceUploadBatch BeginTextureUpload() {
        ResourceUploadBatch batch(Render::Device);
        batch.Begin();
        return batch;
    }

    void EndTextureUpload(ResourceUploadBatch& batch, ID3D12CommandQueue* queue) {
        auto task = batch.End(queue);
        task.wait();
    }

    void GetTexturesForModel(ModelID id, Set<TexID>& ids) {
        if (id == ModelID::None) return;

        auto& model = Resources::GetModel(id);

        for (int16 i = 0; i < model.TextureCount; i++) {
            auto tid = Resources::LookupModelTexID(model, i);
            ids.insert(tid);

            // Also load effect clip frames
            auto& eclip = Resources::GetEffectClip(tid);
            Seq::insert(ids, eclip.VClip.GetFrames());
            auto& crit = Resources::GetEffectClip(eclip.CritClip);
            Seq::insert(ids, crit.VClip.GetFrames());
        }
    }

    Set<TexID> GetLevelModelTextures(const Inferno::Level& level) {
        Set<TexID> ids;

        // Textures for each object
        for (auto& object : level.Objects) {
            switch (object.Type) {
                case ObjectType::Robot:
                {
                    auto& info = Resources::GetRobotInfo(object.ID);
                    GetTexturesForModel(info.Model, ids);

                    if (object.Render.Model.TextureOverride != LevelTexID::None) {
                        auto id = Resources::LookupTexID(object.Render.Model.TextureOverride);
                        ids.insert(id);
                    }

                    break;
                }
                default:
                    if (object.Render.Type == RenderType::Model)
                        GetTexturesForModel(object.Render.Model.ID, ids);
                    break;
            }
        }

        return ids;
    }

    Set<TexID> GetLevelSegmentTextures(const Inferno::Level& level) {
        Set<TexID> ids;

        auto insertEClip = [&ids](EClipID id) {
            if (id == EClipID::None) return;
            auto& clip = Resources::GetEffectClip(id);
            Seq::insert(ids, clip.VClip.GetFrames());
        };

        for (auto& seg : level.Segments) {
            for (auto& sideId : SideIDs) {
                auto& side = seg.GetSide(sideId);
                if (!seg.SideHasConnection(sideId) || seg.SideIsWall(sideId)) {
                    ids.insert(Resources::LookupTexID(side.TMap));
                    auto& eclip = Resources::GetEffectClip(side.TMap);
                    Seq::insert(ids, eclip.VClip.GetFrames());
                    insertEClip(eclip.CritClip);
                    insertEClip(eclip.DestroyedEClip);
                }

                if (side.HasOverlay()) {
                    ids.insert(Resources::LookupTexID(side.TMap2));
                    auto& eclip = Resources::GetEffectClip(side.TMap2);
                    Seq::insert(ids, eclip.VClip.GetFrames());
                    auto& destroyed = Resources::GetVideoClip(eclip.DestroyedVClip);
                    Seq::insert(ids, destroyed.GetFrames());
                    insertEClip(eclip.CritClip);
                    insertEClip(eclip.DestroyedEClip);

                    ids.insert(Resources::LookupTexID(eclip.DestroyedTexture));
                }

                // Door clips
                if (auto wall = level.Walls.TryGetWall(side.Wall)) {
                    auto& wclip = Resources::GetDoorClip(wall->Clip);
                    auto wids = Seq::map(wclip.GetFrames(), Resources::LookupTexID);
                    Seq::insert(ids, wids);
                }
            }
        }

        return ids;
    }

    Set<TexID> GetGameplayTextures() {
        Set<TexID> ids;

        // Load all weapon clips and models
        for (auto& weapon : Resources::GameData.Weapons) {
            ids.insert(weapon.BlobBitmap);
            ids.insert(weapon.HiresIcon);
            ids.insert(weapon.Icon);

            GetTexturesForModel(weapon.Model, ids);
        }

        // Load all vclips
        for (auto& vclip : Resources::GameData.VClips) {
            Seq::insert(ids, vclip.GetFrames());
        }

        // Load robots from bosses
        //for (auto& obj : Game::Level.Objects) {
        //    if (obj.IsRobot()) {
        //        for (auto& rid : Resources::GetRobotInfo(obj).GatedRobots) {
        //            auto& ri = Resources::GetRobotInfo(rid);
        //            GetTexturesForModel(ri.Model, ids);
        //        }
        //    }
        //}

        // Load robots from matcens
        //for (auto& matcen : Game::Level.Matcens) {
        //    for (auto& rid : matcen.GetEnabledRobots()) {
        //        auto& ri = Resources::GetRobotInfo(rid);
        //        GetTexturesForModel(ri.Model, ids);
        //    }
        //}

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

        //{
        //    auto& matcen = Resources::GetVideoClip(VClipID::Matcen);
        //    Seq::insert(vclips, matcen.GetFrames()); // Always load matcen effect
        //}
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
    void ExpandMask(const PigEntry& bmp, List<uint8>& data) {
        auto getPixel = [&](int x, int y) -> uint8& {
            if (x < 0) x += bmp.Width;
            if (x > bmp.Width - 1) x -= bmp.Width;
            if (y < 0) y += bmp.Height;
            if (y > bmp.Height - 1) y -= bmp.Height;

            int offset = bmp.Width * y + x;
            return data[offset];
        };

        auto markMask = [](uint8& dst) { dst = 128; };

        // row pass. starts at top left.
        for (int y = 0; y < bmp.Height; y++) {
            for (int x = 0; x < bmp.Width; x++) {
                auto& px = data[bmp.Width * y + x];
                auto& below = getPixel(x, y + 1);
                auto& above = getPixel(x, y - 1);
                // row below is masked and this one isn't
                if (below == 255 && px != 255) markMask(px);

                // row above is masked and this one isn't
                if (above == 255 && px != 255) markMask(px);
            }
        }

        // column pass. starts at top left.
        for (int x = 0; x < bmp.Width; x++) {
            for (int y = 0; y < bmp.Height; y++) {
                auto& px = data[bmp.Width * y + x];
                auto& left = getPixel(x - 1, y);
                auto& right = getPixel(x + 1, y);
                // column left is masked and this one isn't
                if (left == 255 && px != 255) markMask(px);

                // column right is masked and this one isn't
                if (right == 255 && px != 255) markMask(px);
            }
        }

        // Update the marked pixels to 255
        for (auto& px : data) {
            if (px > 0) px = 255;
        }
    }

    Option<Material2D> UploadMaterial(ResourceUploadBatch& batch,
                                      const MaterialUpload& upload) {
        if (upload.ID <= TexID::Invalid) return {};
        Material2D material;
        material.ID = upload.ID;
        material.Name = upload.Bitmap->Info.Name;
        material.UploadIndex = Render::Uploads->AllocateIndex();

        // allocate a new heap range for the material
        for (int i = 0; i < Material2D::Count; i++)
            material.Handles[i] = Render::Uploads->GetGpuHandle(material.UploadIndex + i);

        // remove the frame number when loading special textures, as they should share.
        string baseName = material.Name;
        if (auto i = baseName.find("#"); i > 0)
            baseName = baseName.substr(0, i);

        const auto width = upload.Bitmap->Info.Width;
        const auto height = upload.Bitmap->Info.Height;

        //SPDLOG_INFO("Loading texture `{}` to heap index: {}", ti->Name, material.Index);
        if (Settings::Graphics.HighRes) {
            if (auto path = FileSystem::TryFindFile(material.Name + ".dds"))
                material.Textures[Material2D::Diffuse].LoadDDS(batch, *path/*, true*/);

            if (upload.SuperTransparent)
                if (auto path = FileSystem::TryFindFile(baseName + "_st.dds"))
                    material.Textures[Material2D::SuperTransparency].LoadDDS(batch, *path);
        }

        if (!material.Textures[Material2D::Diffuse]) {
            material.Textures[Material2D::Diffuse].Load(batch, upload.Bitmap->Data.data(), width, height, Convert::ToWideString(material.Name));
            //if (upload.Bitmap->Info.Transparent) {
            //    List<Palette::Color> data = upload.Bitmap->Data; // copy mask, as modifying the original would affect collision
            //    //ExpandDiffuse(upload.Bitmap->Info, data);
            //    material.Textures[Material2D::Diffuse].Load(batch, data.data(), width, height, Convert::ToWideString(material.Name));
            //}
            //else {
            //    material.Textures[Material2D::Diffuse].Load(batch, upload.Bitmap->Data.data(), width, height, Convert::ToWideString(material.Name));
            //}
        }

        if (!material.Textures[Material2D::SuperTransparency] && upload.SuperTransparent) {
            List<uint8> mask;
            mask.resize(upload.Bitmap->Mask.size());
            for (size_t i = 0; i < upload.Bitmap->Mask.size(); i++)
                mask[i] = upload.Bitmap->Mask[i].r;

            ExpandMask(upload.Bitmap->Info, mask);
            material.Textures[Material2D::SuperTransparency].Load(batch, mask.data(), width, height, Convert::ToWideString(material.Name), true, DXGI_FORMAT_R8_UNORM);
        }

        if (auto path = FileSystem::TryFindFile(baseName + "_e.dds"))
            material.Textures[Material2D::Emissive].LoadDDS(batch, *path);

        if (auto path = FileSystem::TryFindFile(baseName + "_s.dds"))
            material.Textures[Material2D::Specular].LoadDDS(batch, *path);

        //bool genMaps = (Resources::IsLevelTexture(material.ID) || Resources::IsObjectTexture(material.ID)) && Settings::Inferno.GenerateMaps;

        //if (!material.Textures[Material2D::Specular] && genMaps) {
        //    auto specular = CreateSpecularMap(*upload.Bitmap);
        //    material.Textures[Material2D::Specular].Load(batch, specular.data(), width, height, Convert::ToWideString(material.Name), true, DXGI_FORMAT_R8_UNORM);
        //}

        //if (!material.Textures[Material2D::Normal] && genMaps) {
        //    auto normal = CreateNormalMap(*upload.Bitmap);
        //    material.Textures[Material2D::Normal].Load(batch, normal.data(), width, height, Convert::ToWideString(material.Name), true, DXGI_FORMAT_R8G8B8A8_UNORM);
        //}

        for (uint i = 0; i < std::size(material.Textures); i++) {
            auto handle = Render::Uploads->GetCpuHandle(material.UploadIndex + i);
            Texture2D* texture = nullptr;

            if (material.Textures[i]) {
                texture = &material.Textures[i];
            }
            else {
                if (i == Material2D::Normal)
                    texture = &Render::StaticTextures->Normal;
                else if (i == Material2D::Emissive || i == Material2D::Specular)
                    texture = &Render::StaticTextures->White;
                else
                    texture = &Render::StaticTextures->Black;
            }

            texture->CreateShaderResourceView(handle);
        }

        return material;
    }

    Material2D UploadBitmap(ResourceUploadBatch& batch, const string& name, const Texture2D& defaultTex) {
        Material2D material;
        material.UploadIndex = Render::Uploads->AllocateIndex();

        // allocate a new heap range for the material
        for (int i = 0; i < Material2D::Count; i++)
            material.Handles[i] = Render::Uploads->GetGpuHandle(material.UploadIndex + i);

        material.Name = name;
        if (auto path = FileSystem::TryFindFile(name + ".dds"))
            material.Textures[Material2D::Diffuse].LoadDDS(batch, *path, true);

        // Set default secondary textures
        for (uint i = 0; i < std::size(material.Textures); i++) {
            auto handle = Render::Uploads->GetCpuHandle(material.UploadIndex + i);
            auto texture = material.Textures[i] ? &material.Textures[i] : &defaultTex;
            texture->CreateShaderResourceView(handle);
        }

        return material;
    }

    Material2D UploadOutrageMaterial(ResourceUploadBatch& batch,
                                     const Outrage::Bitmap& bitmap,
                                     const Texture2D& defaultTex) {
        Material2D material;
        material.UploadIndex = Render::Uploads->AllocateIndex();
        assert(!bitmap.Mips.empty());

        // allocate a new heap range for the material
        for (int i = 0; i < Material2D::Count; i++)
            material.Handles[i] = Render::Uploads->GetGpuHandle(material.UploadIndex + i);

        material.Name = bitmap.Name;
        material.Textures[Material2D::Diffuse].Load(batch, bitmap.Mips[0].data(), bitmap.Width, bitmap.Height, Convert::ToWideString(bitmap.Name));

        // Set default secondary textures
        for (uint i = 0; i < std::size(material.Textures); i++) {
            auto handle = Render::Uploads->GetCpuHandle(material.UploadIndex + i);
            auto texture = material.Textures[i] ? &material.Textures[i] : &defaultTex;
            texture->CreateShaderResourceView(handle);
        }

        return material;
    }

    //Material2D UploadBitmap(ResourceUploadBatch& batch,
    //                        string_view name,
    //                        const Bitmap2D& bitmap,
    //                        const Texture2D& defaultTex) {
    //    Material2D material;
    //    material.UploadIndex = Render::Uploads->AllocateIndex();
    //    material.Name = name;

    //    // allocate a new heap range for the material
    //    for (int i = 0; i < Material2D::Count; i++)
    //        material.Handles[i] = Render::Uploads->GetGpuHandle(material.UploadIndex + i);

    //    material.Textures[Material2D::Diffuse].Load(batch, bitmap.Data.data(), bitmap.Width, bitmap.Height, Convert::ToWideString(name), false, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    //    // Set default secondary textures
    //    for (uint i = 0; i < std::size(material.Textures); i++) {
    //        auto handle = Render::Uploads->GetCpuHandle(material.UploadIndex + i);
    //        auto texture = material.Textures[i] ? &material.Textures[i] : &defaultTex;
    //        texture->CreateShaderResourceView(handle);
    //    }

    //    return material;
    //}

    class MaterialUploadWorker : public WorkerThread {
        MaterialLibrary* _lib;

    public:
        MaterialUploadWorker(MaterialLibrary* lib) : WorkerThread("material uploader"), _lib(lib) {}

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

                if (auto material = UploadMaterial(batch, upload))
                    uploads.emplace_back(std::move(material.value()));
            }

            EndTextureUpload(batch, Render::Adapter->AsyncBatchUploadQueue->Get());

            //SPDLOG_INFO("Moving {} uploads to pending copies", uploads.size());
            {
                std::scoped_lock lock(WorkerMutex);
                for (auto& upload : uploads)
                    _lib->_pendingCopies.push_back(std::move(upload)); // copies are performed on main thread
            }

            if (!uploads.empty()) {
                //SPDLOG_INFO("Loaded {} textures on background thread", uploads.size());
                Render::Adapter->PrintMemoryUsage();
                Render::Uploads->GetFreeDescriptors();
            }
        }
    };

    MaterialLibrary::MaterialLibrary(size_t size)
        : _materials(size), _keepLoaded(size) {
        assert(size >= 3000); // Reserved textures at id 2900
        LoadDefaults();
        _worker = MakePtr<MaterialUploadWorker>(this);
        _worker->Start();
    }

    void MaterialLibrary::Shutdown() {
        _worker.reset();
    }

    void MaterialLibrary::LoadMaterials(span<const TexID> tids, bool forceLoad, bool keepLoaded) {
        // Pre-scan materials, as starting an upload batch causes a stall
        if (!forceLoad && !HasUnloadedTextures(tids)) return;

        //Stopwatch time;
        List<Material2D> uploads;
        auto batch = BeginTextureUpload();

        for (auto& id : tids) {
            if (auto upload = PrepareUpload(id, forceLoad)) {
                if (auto material = UploadMaterial(batch, *upload))
                    uploads.emplace_back(std::move(material.value()));
            }

            if (id > TexID::None) _keepLoaded[(int)id] = keepLoaded;
        }

        SPDLOG_INFO("Loading {} textures", uploads.size());
        EndTextureUpload(batch, Render::Adapter->BatchUploadQueue->Get());
        MoveUploads(uploads, _materials);

        //SPDLOG_INFO("LoadMaterials: {:.3f}s", time.GetElapsedSeconds());
        Render::Adapter->PrintMemoryUsage();
        Render::Uploads->GetFreeDescriptors();
    }

    void MaterialLibrary::LoadMaterialsAsync(span<const TexID> ids, bool forceLoad, bool keepLoaded) {
        if (!forceLoad && !HasUnloadedTextures(ids)) return;

        for (auto& id : ids) {
            if (auto upload = PrepareUpload(id, forceLoad))
                _requestedUploads.Add(*upload);

            if (id > TexID::None) _keepLoaded[(int)id] = keepLoaded;
        }

        _worker->Notify();
    }

    Option<MaterialUpload> MaterialLibrary::PrepareUpload(TexID id, bool forceLoad) {
        if (!Seq::inRange(_materials, (int)id)) return {};

        auto& slot = _materials[(int)id];
        if (!forceLoad && slot.State == TextureState::Resident) return {};
        if (slot.State == TextureState::PagingIn) return {};

        auto& bitmap = Resources::GetBitmap(id);
        if (bitmap.Info.Width == 0 || bitmap.Info.Height == 0)
            return {};

        MaterialUpload upload;
        upload.Bitmap = &bitmap;
        upload.ID = id;
        upload.SuperTransparent = Resources::GetTextureInfo(id).SuperTransparent;
        slot.State = TextureState::PagingIn;
        return upload;
    }

    void MaterialLibrary::Dispatch() {
        if (!_pendingCopies.empty()) {
            SPDLOG_INFO("Moving {} uploaded textures", _pendingCopies.size());
            Render::Adapter->WaitForGpu();
            std::scoped_lock lock(WorkerMutex);
            MoveUploads(_pendingCopies, _materials);
            _pendingCopies.clear();
            Render::Uploads->GetFreeDescriptors();
        }
    }

    const Material2D& MaterialLibrary::Get(EClipID id, double time, bool critical) const {
        auto& eclip = Resources::GetEffectClip(id);
        if (eclip.TimeLeft > 0)
            time = eclip.VClip.PlayTime - eclip.TimeLeft;

        TexID tex = eclip.VClip.GetFrame(time);
        if (critical && eclip.CritClip != EClipID::None) {
            auto& crit = Resources::GetEffectClip(eclip.CritClip);
            tex = crit.VClip.GetFrame(time);
        }

        return Get(tex);
    }

    const Material2D& MaterialLibrary::Get(LevelTexID tid) const {
        auto id = Resources::LookupTexID(tid);
        return Get(id);
    }

    void MaterialLibrary::LoadLevelTextures(const Inferno::Level& level, bool force) {
        SPDLOG_INFO("Load level textures. Force {}", force);
        Render::Adapter->WaitForGpu();
        ranges::fill(_keepLoaded, false);
        auto ids = GetLevelTextures(level, PreloadDoors);

        //for (auto& id : ids)
        //    EnableProcedural(id);

        auto tids = Seq::ofSet(ids);
        LoadMaterials(tids, force);
    }

    void MaterialLibrary::LoadTextures(span<const string> names) {
        bool hasUnloaded = false;
        for (auto& name : names) {
            if (!name.empty() && !_namedMaterials.contains(name)) {
                hasUnloaded = true;
                break;
            }
        }

        if (!hasUnloaded) return;
        Render::Adapter->WaitForGpu();

        List<Material2D> uploads;
        auto batch = BeginTextureUpload();

        for (auto& name : names) {
            if (_namedMaterials.contains(name)) continue; // skip loaded
            Material2D material;

            bool outrage = false;

            if (FileSystem::TryFindFile(name + ".dds")) {
                material = UploadBitmap(batch, name, Render::StaticTextures->Black);
            }
            else if (name.ends_with(".oaf") || name.ends_with(".OAF")) {
                if (auto bitmap = Resources::ReadOutrageVClip(name)) {
                    outrage = true;
                    material = UploadOutrageMaterial(batch, *bitmap, Render::StaticTextures->Black);
                }
            }
            else if (auto bitmap = Resources::ReadOutrageBitmap(name)) {
                // Try loading file from D3 data
                outrage = true;
                material = UploadOutrageMaterial(batch, *bitmap, Render::StaticTextures->Black);
            }
            else {
                SPDLOG_INFO("Unknown file type for '{}'", name);
                continue;
            }

            //else if (auto data = Resources::ReadBinaryFile(name); !data.empty()) {
            //    if (name.ends_with(".bbm")) {
            //        auto bbm = ReadBbm(data);
            //        material = UploadBitmap(batch, name, bbm, Render::StaticTextures->Black);
            //    }
            //    else if (name.ends_with(".pcx")) {
            //        auto pcx = ReadPCX(data);
            //        material = UploadBitmap(batch, name, pcx, Render::StaticTextures->Black);
            //    }
            //}

            if (material.Name.empty()) {
                // Add entries that aren't found so they are skipped in future loads
                _namedMaterials[name] = {};
            }
            else {
                _namedMaterials[name] = material.ID = outrage ? GetOutrageTexID() : GetUnusedTexID();
                uploads.emplace_back(std::move(material));
            }
        }

        EndTextureUpload(batch, Render::Adapter->BatchUploadQueue->Get());

        MoveUploads(uploads, _materials);

        //for (auto& upload : uploads) {
        //    auto texId = GetUnusedTexID();
        //    _unpackedMaterials[upload.Name] = texId;
        //    upload.ID = texId;
        //    _materials[(int)texId] = std::move(upload);
        //}
    }

    void MaterialLibrary::LoadOutrageTextures(span<const TexID> indices) {
        bool hasUnloaded = false;
        for (auto& index : indices) {
            if (index < OUTRAGE_TEXID_START) continue;
            if (!Seq::inRange(_materials, (int)index)) continue;
            if (_materials[(int)index].State == TextureState::Vacant) {
                hasUnloaded = true;
                break;
            }

            assert((int)index >= 3000);
        }

        if (!hasUnloaded) return;
        Render::Adapter->WaitForGpu();

        List<Material2D> uploads;
        auto batch = BeginTextureUpload();

        for (auto& index : indices) {
            if (index < OUTRAGE_TEXID_START) continue;
            if (!Seq::inRange(_materials, (int)index)) continue;
            if (_materials[(int)index].State == TextureState::Resident || _materials[(int)index].State == TextureState::PagingIn) continue;

            Material2D material;

            auto entry = Seq::tryItem(Resources::GameTable.Textures, (int)index - (int)Render::OUTRAGE_TEXID_START);
            if (!entry) continue;

            if (entry->FileName.ends_with(".oaf") || entry->FileName.ends_with(".OAF")) {
                if (auto bitmap = Resources::ReadOutrageVClip(entry->FileName)) {
                    material = UploadOutrageMaterial(batch, *bitmap, Render::StaticTextures->Black);
                }
            }
            else if (auto bitmap = Resources::ReadOutrageBitmap(entry->FileName)) {
                // Try loading file from D3 data
                material = UploadOutrageMaterial(batch, *bitmap, Render::StaticTextures->Black);
            }
            else {
                SPDLOG_INFO("Unknown file type for '{}'", entry->FileName);
                continue;
            }

            material.ID = (TexID)index;
            uploads.emplace_back(std::move(material));
        }

        EndTextureUpload(batch, Render::Adapter->BatchUploadQueue->Get());

        MoveUploads(uploads, _materials);
    }

    void MaterialLibrary::LoadGameTextures() {
        Render::Adapter->WaitForGpu();
        auto ids = GetGameplayTextures();
        auto tids = Seq::ofSet(ids);
        LoadMaterials(tids);
    }

    void MaterialLibrary::Reload() {
        List<TexID> ids;

        for (auto& material : _materials) {
            if (material.State == TextureState::Resident)
                ids.push_back(material.ID);
        }

        LoadMaterialsAsync(ids, true);
        Prune();
    }

    void MaterialLibrary::ResetMaterial(Material2D& material) {
        if (material.ID >= TexID(2900) && material.ID < OUTRAGE_TEXID_START) return; // reserved range

        //auto id = material.ID;
        material = { .ID = material.ID }; // mark the material as unused

        // Update the upload to use the new handles
        for (int i = 0; i < Material2D::Count; i++)
            material.Handles[i] = Render::Heaps->Materials.GetGpuHandle((int)material.ID * 5 + i);

        //auto cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5));
        StaticTextures->Missing.CreateShaderResourceView(Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5));
        StaticTextures->Black.CreateShaderResourceView(Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5 + 1));
        StaticTextures->Black.CreateShaderResourceView(Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5 + 2));
        StaticTextures->Black.CreateShaderResourceView(Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5 + 3));
        StaticTextures->Normal.CreateShaderResourceView(Render::Heaps->Materials.GetCpuHandle((int)material.ID * 5 + 4));

        //SPDLOG_INFO("Resetting material {} {}", (int)material.ID, material.Name);
    }

    void MaterialLibrary::Prune() {
        Render::Adapter->WaitForGpu();

        SPDLOG_INFO("Pruning textures");
        auto ids = GetLevelTextures(Game::Level, PreloadDoors);

        for (auto& material : _materials) {
            //if (material.ID <= TexID::Invalid || ids.contains(material.ID) || material.State != TextureState::Resident) continue;
            if (ids.contains(material.ID)) continue;
            if (_keepLoaded[(int)material.ID]) continue;
            if (material.ID >= OUTRAGE_TEXID_START) break;
            ResetMaterial(material);
        }

        Render::Adapter->PrintMemoryUsage();
        Render::LevelChanged = true; // To trigger refresh of material cache
    }

    void MaterialLibrary::Unload() {
        SPDLOG_INFO("Unloading all textures");
        Render::Adapter->WaitForGpu();

        for (auto& material : _materials) {
            if (material.ID <= TexID::Invalid) continue;
            if (material.ID >= OUTRAGE_TEXID_START) break; // Don't unload d3 textures
            ResetMaterial(material);
        }

        _looseTexId = LOOSE_TEXID_START;
        _outrageTexId = OUTRAGE_TEXID_START;
        Render::Adapter->PrintMemoryUsage();
    }

    void MaterialLibrary::UnloadNamedTextures() {
        SPDLOG_INFO("Unloading extended textures");
        Render::Adapter->WaitForGpu();

        for (auto& id : _namedMaterials | views::values) {
            if (id <= TexID::Invalid) continue;
            auto& material = Get(id);
            ResetMaterial(material);
        }

        _namedMaterials.clear();
        _looseTexId = LOOSE_TEXID_START;
        _outrageTexId = OUTRAGE_TEXID_START;
        Render::Adapter->PrintMemoryUsage();
    }

    void MaterialLibrary::LoadDefaults() {
        for (int i = 0; i < _materials.size(); i++) {
            auto& material = _materials[i];
            material.ID = TexID(i);
            ResetMaterial(material);
        }

        for (uint i = 0; i < Material2D::Count; i++) {
            // this makes the dangerous assumption that no other threads will allocate between iterations
            //auto handle = Render::Heaps->Reserved.Allocate();
            auto handle = Render::Heaps->Materials.GetHandle((int)MISSING_MATERIAL * 5 + i);
            auto& material = _materials[(int)MISSING_MATERIAL];
            material.Name = "missing";
            material.Handles[i] = handle.GetGpuHandle();
            material.State = TextureState::Resident;
            material.ID = MISSING_MATERIAL;
            //_defaultMaterial.State = TextureState::Resident;

            if (i == Material2D::Diffuse)
                Render::StaticTextures->Missing.CreateShaderResourceView(handle.GetCpuHandle());
            else if (i == Material2D::Normal)
                Render::StaticTextures->Normal.CreateShaderResourceView(handle.GetCpuHandle());
            else
                Render::StaticTextures->Black.CreateShaderResourceView(handle.GetCpuHandle());
        }

        for (uint i = 0; i < Material2D::Count; i++) {
            //auto handle = Render::Heaps->Reserved.Allocate();
            auto handle = Render::Heaps->Materials.GetHandle((int)WHITE_MATERIAL * 5 + i);
            auto& material = _materials[(int)WHITE_MATERIAL];
            material.Name = "white";
            material.Handles[i] = handle.GetGpuHandle();
            material.State = TextureState::Resident;
            material.ID = WHITE_MATERIAL;

            if (i == Material2D::Diffuse)
                Render::StaticTextures->White.CreateShaderResourceView(handle.GetCpuHandle());
            else if (i == Material2D::Normal)
                Render::StaticTextures->Normal.CreateShaderResourceView(handle.GetCpuHandle());
            else
                Render::StaticTextures->Black.CreateShaderResourceView(handle.GetCpuHandle());
        }

        for (uint i = 0; i < Material2D::Count; i++) {
            auto handle = Render::Heaps->Materials.GetHandle((int)BLACK_MATERIAL * 5 + i);
            //auto handle = Render::Heaps->Reserved.Allocate();
            auto& material = _materials[(int)BLACK_MATERIAL];
            material.Name = "black";
            material.Handles[i] = handle.GetGpuHandle();
            material.State = TextureState::Resident;
            material.ID = BLACK_MATERIAL;

            if (i == Material2D::Normal)
                Render::StaticTextures->Normal.CreateShaderResourceView(handle.GetCpuHandle());
            else
                Render::StaticTextures->Black.CreateShaderResourceView(handle.GetCpuHandle());
        }

        for (uint i = 0; i < Material2D::Count; i++) {
            auto handle = Render::Heaps->Materials.GetHandle((int)SHINY_FLAT_MATERIAL * 5 + i);
            auto& material = _materials[(int)SHINY_FLAT_MATERIAL];
            material.Name = "white";
            material.Handles[i] = handle.GetGpuHandle();
            material.State = TextureState::Resident;
            material.ID = SHINY_FLAT_MATERIAL;

            if (i == Material2D::Diffuse)
                Render::StaticTextures->White.CreateShaderResourceView(handle.GetCpuHandle());
            else if (i == Material2D::Normal)
                Render::StaticTextures->Normal.CreateShaderResourceView(handle.GetCpuHandle());
            else if (i == Material2D::Specular)
                Render::StaticTextures->White.CreateShaderResourceView(handle.GetCpuHandle());
            else
                Render::StaticTextures->Black.CreateShaderResourceView(handle.GetCpuHandle());
        }
    }
}
