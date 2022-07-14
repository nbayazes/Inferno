#pragma once

#include "Types.h"
#include "OutrageBitmap.h"
#include "Graphics/Buffers.h"
#include "Concurrent.h"
#include "Convert.h"
#include "Settings.h"

namespace Inferno {
    // Handle to a material, which is a combination of textures and has a GPU handle
    enum class MaterialHandle { Missing = 0, None = -1 };

    struct RuntimeTextureInfo : public Outrage::TextureInfo {
        MaterialHandle BitmapHandle = MaterialHandle::None;
        MaterialHandle DestroyedHandle = MaterialHandle::None;
        List<MaterialHandle> FrameHandles;
        float FrameTime = 1;
        bool Used = false;
        bool PingPong = false;
        int VClip = -1; // index to Resources::VClips

        MaterialHandle GetFrame(int offset, float time) const {
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
        std::unordered_map<string, Ref<Texture2D>, std::hash<string>, InvariantEquals> _textures;

        std::mutex _lock;

        List<Material> _materials;

        Material _defaultMaterial;
        Material _whiteMaterial;
        Ref<Texture2D> White, Black, Missing;
    public:

        //void Prune() {
        //    std::scoped_lock lock(_lock);

        //    for (auto& tex : _textures) {
        //        if (tex.use_count() == 1) {
        //            tex.reset();
        //            SPDLOG_INFO("Freeing texture");
        //        }
        //    }
        //}

        TextureGpuCache() {
            LoadDefaults();
            _textures.reserve(3000);
            _materials.reserve(3000);
        }

        void LoadDefaults();

        void SetResourceHandles(Material& m) {
            auto heapStartIndex = Render::Heaps->Shader.AllocateIndex();
            m.Handle = Render::Heaps->Shader.GetGpuHandle(heapStartIndex);

            for (int i = 0; i < Material::Count; i++) {
                if (!m.Textures[i]) m.Textures[i] = i == 0 ? Missing : Black;
                auto cpuHandle = Render::Heaps->Shader.GetCpuHandle(heapStartIndex + i);
                m.Textures[i]->CreateShaderResourceView(cpuHandle);
            }
        }

        Ref<Texture2D> FindTexture(const string& name) {
            if (_textures.contains(name))
                return _textures[name];

            return _textures[name] = MakeRef<Texture2D>();
        }

        void Load(DirectX::ResourceUploadBatch& batch, MaterialHandle& handle, const Outrage::Bitmap& bitmap) {
            auto& m = FetchOrAllocMaterial(handle);
            bool same = m.Name == bitmap.Name;
            m.Name = bitmap.Name;
            m.Textures[Material::Diffuse] = FindTexture(m.Name);
            m.Textures[Material::Diffuse]->Load(batch, bitmap.Mips[0].data(), bitmap.Width, bitmap.Height, Convert::ToWideString(bitmap.Name));
            //SPDLOG_INFO("Uploading to GPU: {}", m.Name);

            // todo: load specular if present
            // todo: generate mipmaps if not present (and if flag is set?)
            if (!same)
                SetResourceHandles(m);
        }

        // Loads the textures for a material based on the input
        //void Load(DirectX::ResourceUploadBatch& batch, MaterialHandle& handle, string_view fileName) {
        //    auto& m = FetchOrAllocMaterial(handle);
        //    string baseName = String::NameWithoutExtension(fileName);

        //    if (Settings::HighRes) {
        //        if (auto path = FileSystem::TryFindFile(baseName + ".DDS"))
        //            m.Textures[Material::Diffuse] = Upload(batch, *path);
        //    }

        //    // Pig textures
        //    if (!m.Textures[Material::Diffuse] && m.PigID > TexID::None) {
        //        auto& bmp = Resources::ReadBitmap(m.PigID);
        //        m.Textures[Material::Diffuse] = Upload(batch, bmp);
        //    }

        //    // remove the frame number when loading special textures, as they should share.
        //    if (auto i = baseName.find("#"); i > 0)
        //        baseName = baseName.substr(0, i);

        //    if (auto path = FileSystem::TryFindFile(baseName + "_e.DDS"))
        //        m.Textures[Material::Emissive] = Upload(batch, *path);

        //    if (auto path = FileSystem::TryFindFile(baseName + "_s.DDS"))
        //        m.Textures[Material::Specular] = Upload(batch, *path);

        //    SetResourceHandles(m);
        //};

        void LoadTextures(span<RuntimeTextureInfo> textures, bool reload = false);

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(MaterialHandle h) {
            if (!Seq::inRange(_materials, (int)h)) return _defaultMaterial.Handle;
            return _materials[(int)h].Handle;
        }

    private:

        //Ref<Texture2D> Upload(DirectX::ResourceUploadBatch& batch, const PigBitmap& bitmap) {
        //    auto tex = Alloc();
        //    tex->Load(batch, bitmap.Data.data(), bitmap.Width, bitmap.Height, Convert::ToWideString(bitmap.Name));
        //    return tex;
        //}

        //Ref<Texture2D> Upload(DirectX::ResourceUploadBatch& batch, filesystem::path ddsPath) {
        //    auto tex = Alloc();
        //    tex->LoadDDS(batch, ddsPath);
        //    return tex;
        //}

        //Ref<Texture2D> Alloc() {
        //    std::scoped_lock lock(_lock);

        //    for (auto& tex : _textures) {
        //        if (!tex) {
        //            return tex = MakeRef<Texture2D>();
        //        }
        //    }

        //    return _textures.emplace_back(MakeRef<Texture2D>());
        //}

        Material& FetchOrAllocMaterial(MaterialHandle& handle) {
            if (Seq::inRange(_materials, (int)handle))
                return _materials[(int)handle]; // Already exists

            for (int i = 0; i < _materials.size(); i++) {
                if (_materials[i].Name.empty()) {
                    handle = MaterialHandle(i);
                    return _materials[i]; // Unused existing
                }
            }

            handle = (MaterialHandle)_materials.size();
            return _materials.emplace_back(); // New
        }
    };

    class TextureCache {
        List<RuntimeTextureInfo> _textures;
        TextureGpuCache _gpu;
        RuntimeTextureInfo _defaultTexture{};
    public:

        TextureCache() {
            _textures.reserve(3000);
        }

        void Free(string);

        // Resolves resource handle name into texture info ids
        // Used by level geometry
        int Resolve(string name) {
            for (int i = 0; i < _textures.size(); i++) {
                if (String::InvariantEquals(_textures[i].Name, name))
                    return i; // Already loaded
            }

            for (auto& tex : Resources::GameTable.Textures) {
                if (String::InvariantEquals(tex.Name, name)) {
                    return AllocTextureInfo({ tex });
                }
            }

            return -1;
        }

        // Resolves a file name to a texture info id
        // Used by robots
        int ResolveFileName(string fileName) {
            for (int i = 0; i < _textures.size(); i++) {
                if (String::InvariantEquals(_textures[i].FileName, fileName))
                    return i; // Already exists
            }

            for (auto& tex : Resources::GameTable.Textures) {
                if (String::InvariantEquals(tex.FileName, fileName))
                    return AllocTextureInfo({ tex });
            }

            if (auto id = ResolveVClip(fileName); id != -1)
                return id;

            return -1;
        }

        const RuntimeTextureInfo& GetTextureInfo(int handle) {
            if (Seq::inRange(_textures, handle)) {
                return _textures[handle];
            };

            return _defaultTexture;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetResource(const RuntimeTextureInfo& info) {
            if (info.FrameHandles.empty())
                return _gpu.GetGpuHandle(info.BitmapHandle);

            return _gpu.GetGpuHandle(info.GetFrame(0, 0));
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetResource(int handle) {
            if (Seq::inRange(_textures, handle)) {
                auto& info = _textures[handle];
                return GetResource(info);
            }

            return _gpu.GetGpuHandle(MaterialHandle::Missing);
        }

        // Uploads any pending textures to the GPU
        void MakeResident() {
            _gpu.LoadTextures(_textures);
        }

        void Reload() {
            // old materials are not being removed / reused
            _gpu.LoadTextures(_textures, true);
            //_gpu.Prune();
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
        //int FindTextureInfoByName(string name) {
        //    for (int i = 0; i < _textures.size(); i++) {
        //        if (String::InvariantEquals(_textures[i].Name, name))
        //            return i;
        //    }

        //    return -1;
        //}

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
            int index = -1;

            // Find unused slot
            for (int i = 0; i < _textures.size(); i++) {
                if (!_textures[i].Used) {
                    _textures[i] = ti;
                    _textures[i].Used = true;
                    index = i;
                    break;
                }
            }

            if (ti.IsAnimated()) {
                for (int id = 0; id < Resources::VClips.size(); id++) {
                    auto& vclip = Resources::VClips[id];
                    if (vclip.FileName == ti.FileName)
                        ti.VClip = id;
                }
            }

            if (index == -1) {
                // Add new slot
                ti.Used = true;
                index = (int)_textures.size();
                _textures.emplace_back(std::move(ti));
            }

            return index;
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

        int ResolveVClip(string frameName) {
            //if (auto id = FindVClip(name); id != -1)
            //    return id; // Already loaded
            //if (auto id = FindTextureInfoByName(frameName); id != -1)
            //    return id; // Already loaded

            for (int id = 0; id < Resources::VClips.size(); id++) {
                auto& vclip = Resources::VClips[id];
                for (auto& frame : vclip.Frames) {
                    if (String::InvariantEquals(frame.Name, frameName)) {
                        RuntimeTextureInfo ti;
                        ti.FileName = frame.Name;
                        ti.VClip = id;
                        return AllocTextureInfo(std::move(ti));
                    }
                }
            }

            return -1;
        }

        //// Resolves 
        //int ResolveTextureEntry(string name) {
        //    if (auto id = FindTextureInfoByName(name); id != -1)
        //        return id; // Already loaded

        //    for (auto& tex : Resources::GameTable.Textures) {
        //        if (String::InvariantEquals(tex.Name, name)) {
        //            return AllocTextureInfo({ tex });
        //        }
        //    }

        //    return -1;
        //}

        //int ResolveFileName(string fileName) {
        //    if (auto id = FindTextureInfoByFileName(fileName); id != -1)
        //        return id; // Already loaded

        //    for (auto& tex : Resources::GameTable.Textures) {
        //        if (String::InvariantEquals(tex.FileName, fileName)) {
        //            return AllocTextureInfo({ tex });
        //        }
        //    }

        //    return -1;
        //}
    };
};