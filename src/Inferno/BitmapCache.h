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
    enum class MaterialHandle { Bad = 0, None = -1 };

    // Indirection for D3 named materials or free resources
    struct ResourceHandle {
        string Name;
        int TextureInfoID = -1;
        int VClipInfoID = -1;
    };

    struct RuntimeTextureInfo : public Outrage::TextureInfo {
        MaterialHandle BitmapHandle = MaterialHandle::None;
        MaterialHandle DestroyedHandle = MaterialHandle::None;
        List<MaterialHandle> Frames;
        float FrameTime = 1;
        bool Used = false;
        bool PingPong = false;
    };

    struct RuntimeVClip : public Outrage::VClip {
        bool Used = false;
        List<MaterialHandle> Handles;

        MaterialHandle GetFrame(int offset, float time) {
            auto frames = (int)Frames.size();
            auto frameTime = FrameTime / frames;
            auto frame = int(time / frameTime) + offset;

            if (PingPong) {
                frame %= frames * 2;

                if (frame >= frames)
                    frame = (frames - 1) - (frame % frames);
                else
                    frame %= frames;

                return Handles[frame];
            }
            else {
                return Handles[frame % frames];
            }
        };
    };

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

        bool Used;
        int Tablefile = -1;
        string Name; // Tablefile entry name
        string FileName; // Bitmap file
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



    // Tracks textures uploaded to the GPU
    class TextureGpuCache {
        Texture2D _black, _white, _purple;
        List<Ref<Texture2D>> _textures;
        //ConcurrentList<Ref<Texture2D>> _pendingCopies;
        //ConcurrentList<TextureUpload> _requestedUploads;
        std::mutex _lock;

        List<Material> _materials;
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

        // Loads the textures for a material based on the input
        MaterialHandle Load(Material&& m) {
            auto batch = BeginUpload();

            m.Textures[Material::Diffuse] = Missing;
            m.Textures[Material::Mask] = Black;
            m.Textures[Material::Emissive] = Black;
            m.Textures[Material::Specular] = Black;

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
            if (!m.Textures[Material::Diffuse] && !m.FileName.empty()) {
                if (auto bmp = Resources::ReadOutrageBitmap(m.FileName)) {
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

            EndUpload(batch);

            // Set resource handles
            auto heapStartIndex = Render::Heaps->Shader.AllocateIndex();
            m.Handle = Render::Heaps->Shader.GetGpuHandle(heapStartIndex);

            for (int i = 0; i < Material::Count; i++) {
                auto cpuHandle = Render::Heaps->Shader.GetCpuHandle(heapStartIndex + i);
                m.Textures[i]->CreateShaderResourceView(cpuHandle);
            }

            _materials.emplace_back(m);
            return MaterialHandle(_materials.size() - 1);
        };

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(MaterialHandle h) {
            return _materials[(int)h].Handle;
        }

    private:

        Ref<Texture2D> Upload(DirectX::ResourceUploadBatch& batch, const Outrage::Bitmap& bitmap) {
            //GpuTextureHandle handle;
            //handle.HeapIndex = Render::Heaps->Shader.AllocateIndex();
            //handle.GpuHandle = Render::Heaps->Shader.GetGpuHandle(handle.HeapIndex);

            auto tex = Alloc();
            tex->Load(batch, bitmap.Mips.data(), bitmap.Width, bitmap.Height, Convert::ToWideString(bitmap.Name));
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
                    return tex;
                }
            }

            return _textures.emplace_back();
        }
    };

    class TextureCache {
        List<RuntimeVClip> _vclips;
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
        void Resolve(ResourceHandle& h) {
            if (!h.Name.empty()) {
                if (auto id = ResolveTexture(h.Name); id != -1) {
                    h.TextureInfoID = id;
                    return;
                }

                if (auto id = ResolveVClip(h.Name); id != -1) {
                    h.VClipInfoID = id;
                    return;
                }
            }
            //else {
            //    auto& lti = Resources::GetLevelTextureInfo(LevelTexID(0));
            //    lti.DestroyedTexture; // needs dynamic resolution. if state destroyed ->
            //    Resources::GetTextureInfo(PigID);
            //}
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetResource(const ResourceHandle& handle) {
            if (Seq::inRange(_vclips, handle.VClipInfoID)) {
                auto& vc = _vclips[handle.VClipInfoID];
                return _gpu.GetGpuHandle(vc.GetFrame(0, 0));
            }
            else if (Seq::inRange(_textures, handle.TextureInfoID)) {
                // not dynamic
                return _gpu.GetGpuHandle(_textures[handle.TextureInfoID].BitmapHandle);
            }
            //else {
            //    auto& entry = Resources::GetTextureInfo(handle.PigID);
            //    //entry.ID;
            //    return _materials[0].Handle;
            //}
        }


        //MaterialHandle Find(string);
        //MaterialHandle Find(TexID);


    private:
        //MaterialHandle Alloc(string); // D3 texture
        //MaterialHandle Alloc(TexID); // D1 / D2 texture

        //MaterialHandle Alloc();

        // Handle GetBitmap(texHandle, frame, procedural)
        // - if animated, page in vclip
        // - use frame based on time, the bitmap stores its handle

        // VClips expand into multiple frames
        void PageInVClip(int handle) {
            assert(Seq::inRange(_vclips, handle));

            auto& vclip = _vclips[handle];
            vclip.Handles.resize(vclip.Frames.size());

            // Load and assign the handles for each frame
            for (int i = 0; i < vclip.Frames.size(); i++) {
                //auto handle = Alloc(vclip.Frames[i].Name);
                Material material{ .Name = vclip.Frames[i].Name };
                vclip.Handles[i] = _gpu.Load(std::move(material));
                //Load(material);
                //vclip.Handles[i] = handle;
                //Load(Get(handle));
            }
        }

        // Returns the handle to an already loaded texture bitmap.
        // Returns the cycled frame of animated textures
        MaterialHandle GetTextureBitmap(int handle, int frame) {
            if (!Seq::inRange(_textures, (int)handle))
                return MaterialHandle::Bad;

            auto& mat = _textures[(int)handle];
            if (!mat.Used)
                return MaterialHandle::Bad;

            if (mat.IsAnimated()) {
                if (!Seq::inRange(_vclips, (int)mat.BitmapHandle))
                    return MaterialHandle::Bad;

                auto& vclip = _vclips[(int)mat.BitmapHandle];
                return vclip.GetFrame(0, 0 /*Game::Time*/);
            }
            else {
                return mat.BitmapHandle;
            }
        }

        // Search loaded textures by name, returns -1 if not found
        int FindTextureInfo(string name) {
            for (int i = 0; i < _textures.size(); i++) {
                if (String::InvariantEquals(_textures[i].Name, name))
                    return i;
            }

            return -1;
        }

        // Search vclips by name, returns -1 if not found
        int FindVClip(string name) {
            for (int i = 0; i < _vclips.size(); i++) {
                for (auto& frame : _vclips[i].Frames) {
                    if (String::InvariantEquals(frame.Name, name))
                        return i;
                }
            }

            return -1;
        }

        // Allocs a slot for texture
        int AllocTextureInfo(Outrage::TextureInfo& ti) {
            // Find unused slot
            for (int i = 0; i < _textures.size(); i++) {
                if (!_textures[i].Used) {
                    _textures[i] = { ti };
                    _textures[i].Used = true;
                    return i;
                }
            }

            // Add new slot
            auto& rti = _textures.emplace_back(ti);
            rti.Used = true;
            return int(_textures.size() - 1);
        }

        // Allocs a slot for vclip
        int AllocVClip(Outrage::VClip& vclip) {
            // Find unused slot
            for (int i = 0; i < _vclips.size(); i++) {
                if (!_vclips[i].Used) {
                    _vclips[i] = { vclip };
                    _vclips[i].Used = true;
                    return i;
                }
            }

            // Add new slot
            auto& rti = _vclips.emplace_back(vclip);
            rti.Used = true;
            return int(_vclips.size() - 1);
        }

        int ResolveVClip(string name) {
            if (auto id = FindVClip(name); id != -1)
                return id; // Already loaded

            for (auto& vclip : Resources::VClips) {
                for (auto& frame : vclip.Frames) {
                    if (String::InvariantEquals(frame.Name, name))
                        return AllocVClip(vclip);
                }
            }

            return -1;
        }

        int ResolveTexture(string name) {
            if (auto id = FindTextureInfo(name); id != -1)
                return id; // Already loaded

            for (auto& tex : Resources::GameTable.Textures) {
                if (String::InvariantEquals(tex.FileName, name)) {
                    return AllocTextureInfo(tex);
                }
            }

            return -1;
        }
    };



};