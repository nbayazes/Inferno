#pragma once

#include "Types.h"
#include "OutrageBitmap.h"
#include "Graphics/Buffers.h"
#include "Concurrent.h"
#include "Convert.h"
#include "Settings.h"

namespace Inferno {
    // Handle to an individual bitmap
    // Based on the texture info, handle either points to a vclip or a single frame
    //enum class BitmapHandle { Bad = 0, None = -1 };

    // Handle to a material, which is a combination of textures and has a GPU handle
    enum class MaterialHandle { Missing = 0, None = -1 };

    // Indirection for D3 named materials or free resources
    //struct ResourceHandle {
    //    //string Name;
    //    int TextureInfoID = -1;
    //    //int VClipInfoID = -1;
    //};

    struct RuntimeTextureInfo : public Outrage::TextureInfo {
        MaterialHandle BitmapHandle = MaterialHandle::None;
        MaterialHandle DestroyedHandle = MaterialHandle::None;
        List<MaterialHandle> FrameHandles;
        float FrameTime = 1;
        bool Used = false;
        bool PingPong = false;
        int VClip = -1;

        MaterialHandle GetFrame(int offset, float time) {
            auto frames = (int)FrameHandles.size();
            auto frameTime = FrameTime / frames;
            auto frame = int(time / frameTime) + offset;

            if (PingPong) {
                frame %= frames * 2;

                if (frame >= frames)
                    frame = (frames - 1) - (frame % frames);
                else
                    frame %= frames;

                return FrameHandles[frame];
            }
            else {
                return FrameHandles[frame % frames];
            }
        };
    };

    //struct RuntimeVClip : public Outrage::VClip {
    //    bool Used = false;
    //    List<MaterialHandle> Handles;

    //    MaterialHandle GetFrame(int offset, float time) {
    //        auto frames = (int)Frames.size();
    //        auto frameTime = FrameTime / frames;
    //        auto frame = int(time / frameTime) + offset;

    //        if (PingPong) {
    //            frame %= frames * 2;

    //            if (frame >= frames)
    //                frame = (frames - 1) - (frame % frames);
    //            else
    //                frame %= frames;

    //            return Handles[frame];
    //        }
    //        else {
    //            return Handles[frame % frames];
    //        }
    //    };
    //};

    inline DirectX::ResourceUploadBatch BeginUpload() {
        DirectX::ResourceUploadBatch batch(Render::Device);
        batch.Begin();
        return batch;
    }

    inline void EndUpload(DirectX::ResourceUploadBatch& batch) {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        ComPtr<ID3D12CommandQueue> cmdQueue;
        ThrowIfFailed(Render::Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue)));
        auto task = batch.End(cmdQueue.Get());
        task.wait();
        //return cmdQueue; // unknown why we need to hold onto the queue, but it randomly crashes due to releasing too early
    }


    // TextureInfo GameTextures[2600] - Table texture data. Combine TextureInfo and PigEntry 
    // GameBitmaps[5000] - CPU and GPU texture info. combine pigbitmap and bms_bitmap 

    // Bitmap + Table entry = Material w/ Texture2D

    struct Material {
        enum { Diffuse, Mask, Emissive, Specular, Count };

        //bool Used;
        //int Tablefile = -1;
        string Name;
        //string FileName; // Bitmap file
        TexID PigID = TexID::None; // For D1/D2

        // Handles to resources

        // where is this material on the GPU? Note that materials are 4 consecutive textures
        // This behavior could change based on the shader type
        D3D12_GPU_DESCRIPTOR_HANDLE Handle{};

        // Bitmaps can be shared across materials. Reference them to know when to release.
        // Frames of a vclip share the same mask, emissive and specular
        Array<Ref<Texture2D>, Count> Textures;

        //BitmapHandle Diffuse = BitmapHandle::None;
        //BitmapHandle Mask = BitmapHandle::None;
        //BitmapHandle Emissive = BitmapHandle::None;
        //BitmapHandle Specular = BitmapHandle::None;
    };


    constexpr void FillTexture(span<ubyte> data, ubyte red, ubyte green, ubyte blue, ubyte alpha) {
        for (size_t i = 0; i < data.size() / 4; i++) {
            data[i * 4] = red;
            data[i * 4 + 1] = green;
            data[i * 4 + 2] = blue;
            data[i * 4 + 3] = alpha;
        }
    }

    // Tracks textures uploaded to the GPU
    class TextureGpuCache {
        //Texture2D _black, _white, _purple;
        List<Ref<Texture2D>> _textures;
        //ConcurrentList<Ref<Texture2D>> _pendingCopies;
        //ConcurrentList<TextureUpload> _requestedUploads;
        std::mutex _lock;

        List<Material> _materials;
        Material _defaultMaterial;
        Material _whiteMaterial;
    public:

        //void Load(Texture&);
        //void Load(span<Texture>);
        Ref<Texture2D> White, Black, Missing;

        void Prune() {
            std::scoped_lock lock(_lock);

            for (auto& tex : _textures) {
                if (tex.use_count() == 1) {
                    tex.reset();
                    SPDLOG_INFO("Freeing texture");
                }
            }
        }

        TextureGpuCache() {
            LoadDefaults();
        }

        void LoadDefaults() {
            auto batch = BeginUpload();
            White = MakeRef<Texture2D>();
            Black = MakeRef<Texture2D>();
            Missing = MakeRef<Texture2D>();

            List<ubyte> bmp(64 * 64 * 4);
            FillTexture(bmp, 0, 0, 0, 255);
            Black->Load(batch, bmp.data(), 64, 64, L"black");

            FillTexture(bmp, 255, 255, 255, 255);
            White->Load(batch, bmp.data(), 64, 64, L"white");

            FillTexture(bmp, 255, 0, 255, 255);
            Missing->Load(batch, bmp.data(), 64, 64, L"purple");

            {
                _defaultMaterial.Name = "default";
                _whiteMaterial.Name = "white";

                // Alocates consecutive handles and views for the default materials
                for (uint i = 0; i < Material::Count; i++) {
                    auto handle = Render::Heaps->Reserved.Allocate();
                    if (i == 0)
                        _defaultMaterial.Handle = handle.GetGpuHandle();

                    if (i == 0)
                        Missing->CreateShaderResourceView(handle.GetCpuHandle());
                    else
                        Black->CreateShaderResourceView(handle.GetCpuHandle());
                }

                for (uint i = 0; i < Material2D::Count; i++) {
                    auto handle = Render::Heaps->Reserved.Allocate();
                    if(i == 0)
                        _whiteMaterial.Handle = handle.GetGpuHandle();

                    if (i == 0)
                        White->CreateShaderResourceView(handle.GetCpuHandle());
                    else
                        Black->CreateShaderResourceView(handle.GetCpuHandle());
                }
            }


            EndUpload(batch);
        }

        MaterialHandle SetResourceHandles(Material&& m) {
            auto heapStartIndex = Render::Heaps->Shader.AllocateIndex();
            m.Handle = Render::Heaps->Shader.GetGpuHandle(heapStartIndex);

            for (int i = 0; i < Material::Count; i++) {
                if (!m.Textures[i]) m.Textures[i] = i == 0 ? Missing : Black;
                auto cpuHandle = Render::Heaps->Shader.GetCpuHandle(heapStartIndex + i);
                m.Textures[i]->CreateShaderResourceView(cpuHandle);
            }

            _materials.push_back(std::move(m));
            return MaterialHandle(_materials.size() - 1);
        }

        MaterialHandle Load(DirectX::ResourceUploadBatch& batch, const Outrage::Bitmap& bitmap) {
            Material m{};
            m.Name = bitmap.Name;
            m.Textures[Material::Diffuse] = Upload(batch, bitmap);
            SPDLOG_INFO("Uploading to GPU: {}", m.Name);

            return SetResourceHandles(std::move(m));
        }

        // Loads the textures for a material based on the input
        MaterialHandle Load(DirectX::ResourceUploadBatch& batch, Material&& m) {
            string baseName = String::NameWithoutExtension(m.Name);

            if (Settings::HighRes) {
                if (auto path = FileSystem::TryFindFile(baseName + ".DDS"))
                    m.Textures[Material::Diffuse] = Upload(batch, *path);
            }

            // Pig textures
            if (!m.Textures[Material::Diffuse] && m.PigID > TexID::None) {
                auto& bmp = Resources::ReadBitmap(m.PigID);
                m.Textures[Material::Diffuse] = Upload(batch, bmp);
            }

            // OGF textures
            if (!m.Textures[Material::Diffuse] && !m.Name.empty()) {
                if (auto bmp = Resources::ReadOutrageBitmap(m.Name)) {
                    SPDLOG_INFO("Uploading to GPU: {}", m.Name);
                    m.Textures[Material::Diffuse] = Upload(batch, *bmp);
                    // todo: also load specular from alpha if present
                }
            }

            // remove the frame number when loading special textures, as they should share.
            if (auto i = baseName.find("#"); i > 0)
                baseName = baseName.substr(0, i);

            if (auto path = FileSystem::TryFindFile(baseName + "_e.DDS"))
                m.Textures[Material::Emissive] = Upload(batch, *path);

            if (auto path = FileSystem::TryFindFile(baseName + "_s.DDS"))
                m.Textures[Material::Specular] = Upload(batch, *path);

            return SetResourceHandles(std::move(m));
        };

        void LoadTextures(span<RuntimeTextureInfo> textures) {
            auto batch = BeginUpload();

            for (auto& tex : textures) {
                if (tex.IsAnimated()) {
                    // Load each frame in the animation
                    auto& vclip = Resources::VClips[tex.VClip];
                    for (int i = 0; i < tex.FrameHandles.size(); i++) {
                        auto& handle = tex.FrameHandles[i];
                        if (handle == MaterialHandle::None)
                            handle = Load(batch, vclip.Frames[i]);
                    }
                }
                else if (tex.BitmapHandle == MaterialHandle::None) {
                    if (auto bmp = Resources::ReadOutrageBitmap(tex.FileName)) {
                        tex.BitmapHandle = Load(batch, *bmp);
                    }
                }
            }

            EndUpload(batch);

        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(MaterialHandle h) {
            return _materials[(int)h].Handle;
        }

    private:

        Ref<Texture2D> Upload(DirectX::ResourceUploadBatch& batch, const Outrage::Bitmap& bitmap) {
            //GpuTextureHandle handle;
            //handle.HeapIndex = Render::Heaps->Shader.AllocateIndex();
            //handle.GpuHandle = Render::Heaps->Shader.GetGpuHandle(handle.HeapIndex);

            auto tex = Alloc();
            tex->Load(batch, bitmap.Mips[0].data(), bitmap.Width, bitmap.Height, Convert::ToWideString(bitmap.Name));
            return tex;
        }

        Ref<Texture2D> Upload(DirectX::ResourceUploadBatch& batch, const PigBitmap& bitmap) {
            auto tex = Alloc();
            tex->Load(batch, bitmap.Data.data(), bitmap.Width, bitmap.Height, Convert::ToWideString(bitmap.Name));
            return tex;
        }

        Ref<Texture2D> Upload(DirectX::ResourceUploadBatch& batch, filesystem::path ddsPath) {
            auto tex = Alloc();
            tex->LoadDDS(batch, ddsPath);
            return tex;
        }

        Ref<Texture2D> Alloc() {
            std::scoped_lock lock(_lock);

            for (auto& tex : _textures) {
                if (!tex) {
                    //if (tex.use_count() == 0) {
                        //SPDLOG_INFO("Allocating texture");
                    tex = MakeRef<Texture2D>();
                    return tex;
                }
            }

            return _textures.emplace_back(MakeRef<Texture2D>());
        }
    };

    class TextureCache {
        //List<RuntimeVClip> _vclips;
        List<RuntimeTextureInfo> _textures;
        TextureGpuCache _gpu;

        //Dictionary<TexID, MaterialHandle> _texids;
        //std::unordered_map <string, RuntimeTextureInfo, std::hash<string>, InvariantEquals> _textures;
    public:

        void Free(string);

        //MaterialHandle AllocVClip() {
        //    for (int i = 0; i < _vclips.size(); i++) {
        //        if (!_vclips[i].Used) return MaterialHandle(i);
        //    }

        //    _vclips.emplace_back();
        //    return MaterialHandle(_vclips.size() - 1);
        //}

        //MaterialHandle FindTextureBitmapName(const char* name) {
        //    //if (_names.contains(name)) return _names[name];

        //    int handle = 0;

        //    // scan all textures in the table
        //    for (auto& tex : _textures) {
        //        handle++;
        //        //if (!tex.Used) continue;

        //        if (tex.IsAnimated()) {
        //            // Animated textures are matched by the names of the frames and not the
        //            // name of the animation
        //            //if (auto data = Resources::Descent3Hog->ReadEntry(tex.FileName)) {
        //                //StreamReader r(*data);
        //                //auto vclip = Outrage::VClip::Read(r, tex);
        //                //auto vch = PageInVClip(Outrage::VClip::Read(r, tex));
        //                //auto& vc = _vclips[(int)vch];

        //                //for (auto& frame : vc.Frames) {
        //                //    if (String::InvariantEquals(frame.Name, name)) {
        //                //        // allocate and copy all frames of the vclip
        //                //        //Alloc(name);
        //                //        // use the tex index
        //                //        //auto handle = Alloc(name);

        //                //        //_names[name] = handle;
        //                //        return vch;
        //                //    }
        //                //}
        //            //}
        //        }
        //        else {
        //            if (String::InvariantEquals(tex.Name, name)) {
        //                return tex.BitmapHandle;
        //            }
        //        }
        //    }

        //    return MaterialHandle::None;
        //}

        // loading a texture checks slot if it is animated based on game table
        // - branches regular or animated based on that
        // - polymodels store texture id's, not their names
        // - rescans after textures load
        // - ned_FindTexture->scans game table for name


        // TexIDs are dynamically allocated for D3. Fixed range for D2
        // Textures must be paged in first so a texid exists for them
        //TextureHandle Get(TexID id, int frame = 0) {
        //    if (_texids.contains(id)) return _texids[id];



        //    auto& ti = Resources::GetTextureInfo(id);
        //    if (ti.Animated) {
        //        // lookup frame
        //    }
        //    else {

        //    }
        //}


        // Resolves resource handle name into VClip and Texture ids
        // Should be use as a loading step
        int Resolve(string name) {
            if (auto id = ResolveTexture(name); id != -1) {
                return id;
            }

            if (auto id = ResolveVClip(name); id != -1) {
                return id;
            }

            return -1;
            //else {
            //    auto& lti = Resources::GetLevelTextureInfo(LevelTexID(0));
            //    lti.DestroyedTexture; // needs dynamic resolution. if state destroyed ->
            //    Resources::GetTextureInfo(PigID);
            //}
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetResource(int handle) {
            if (Seq::inRange(_textures, handle)) {
                auto& info = _textures[handle];
                if (info.FrameHandles.empty())
                    return _gpu.GetGpuHandle(info.BitmapHandle);

                return _gpu.GetGpuHandle(info.GetFrame(0, 0));
            }

            return _gpu.GetGpuHandle(MaterialHandle::Missing);
        }


        //MaterialHandle Find(string);
        //MaterialHandle Find(TexID);

        // Uploads any pending textures to the GPU
        void MakeResident() {
            _gpu.LoadTextures(_textures);
        }


    private:
        //MaterialHandle Alloc(string); // D3 texture
        //MaterialHandle Alloc(TexID); // D1 / D2 texture

        //MaterialHandle Alloc();

        // Handle GetBitmap(texHandle, frame, procedural)
        // - if animated, page in vclip
        // - use frame based on time, the bitmap stores its handle

        // Returns the handle to an already loaded texture bitmap.
        // Returns the cycled frame of animated textures
        //MaterialHandle GetTextureBitmap(int handle, int frame) {
        //    if (!Seq::inRange(_textures, (int)handle))
        //        return MaterialHandle::Missing;

        //    auto& mat = _textures[(int)handle];
        //    if (!mat.Used)
        //        return MaterialHandle::Missing;

        //    if (mat.IsAnimated()) {
        //        if (!Seq::inRange(_vclips, (int)mat.BitmapHandle))
        //            return MaterialHandle::Missing;

        //        auto& vclip = _vclips[(int)mat.BitmapHandle];
        //        return vclip.GetFrame(0, 0 /*Game::Time*/);
        //    }
        //    else {
        //        return mat.BitmapHandle;
        //    }
        //}

        // Search loaded textures by name, returns -1 if not found
        int FindTextureInfo(string name) {
            for (int i = 0; i < _textures.size(); i++) {
                if (String::InvariantEquals(_textures[i].FileName, name))
                    return i;
            }

            return -1;
        }

        //// Search vclips by name, returns -1 if not found
        //int FindVClip(string name) {
        //    for (int i = 0; i < _textures.size(); i++) {
        //        for (auto& frame : _textures[i].Frames) {
        //            if (String::InvariantEquals(frame, name))
        //                return i;
        //        }
        //    }

        //    return -1;
        //}

        // Allocs a slot for texture
        int AllocTextureInfo(RuntimeTextureInfo&& ti) {
            // Find unused slot
            for (int i = 0; i < _textures.size(); i++) {
                if (!_textures[i].Used) {
                    _textures[i] = ti;
                    _textures[i].Used = true;
                    return i;
                }
            }

            // Add new slot
            ti.Used = true;
            _textures.emplace_back(ti);
            return int(_textures.size() - 1);
        }

        //// Allocs a slot for vclip
        //int AllocVClip(Outrage::VClip& vclip) {
        //    // Find unused slot
        //    for (int i = 0; i < _vclips.size(); i++) {
        //        if (!_vclips[i].Used) {
        //            _vclips[i] = { vclip };
        //            _vclips[i].Used = true;
        //            return i;
        //        }
        //    }

        //    // Add new slot
        //    auto& rti = _vclips.emplace_back(vclip);
        //    rti.Used = true;
        //    return int(_vclips.size() - 1);
        //}

        int ResolveVClip(string name) {
            //if (auto id = FindVClip(name); id != -1)
            //    return id; // Already loaded
            if (auto id = FindTextureInfo(name); id != -1)
                return id; // Already loaded

            for (int id = 0; id < Resources::VClips.size(); id++) {
                auto& vclip = Resources::VClips[id];
                for (auto& frame : vclip.Frames) {
                    if (String::InvariantEquals(frame.Name, name)) {
                        RuntimeTextureInfo ti;
                        ti.FileName = frame.Name;
                        ti.VClip = id;
                        ti.FrameHandles.resize(vclip.Frames.size(), MaterialHandle::None);
                        return AllocTextureInfo(std::move(ti));
                    }
                }
            }

            return -1;
        }

        int ResolveTexture(string name) {
            if (auto id = FindTextureInfo(name); id != -1)
                return id; // Already loaded

            for (auto& tex : Resources::GameTable.Textures) {
                if (String::InvariantEquals(tex.FileName, name)) {
                    return AllocTextureInfo({ tex });
                }
            }

            return -1;
        }
    };
};