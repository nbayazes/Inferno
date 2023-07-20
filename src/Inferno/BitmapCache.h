#pragma once

#include "Types.h"
#include "OutrageBitmap.h"
#include "Concurrent.h"
#include "OutrageTable.h"
#include "Graphics/GpuResources.h"

namespace Inferno {
    // Handle to a material, which is a combination of textures and has a GPU handle
    enum class MaterialHandle { Missing = 0, None = -1 };

    struct RuntimeTextureInfo : Outrage::TextureInfo {
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
        enum { Diffuse, Mask, Emissive, Specular, Normal, Count };

        string Name;
        TexID PigID = TexID::None; // For D1/D2

        // where is this material on the GPU? Note that materials are 4 consecutive textures
        // This behavior could change based on the shader type
        D3D12_GPU_DESCRIPTOR_HANDLE Handle{};

        // Bitmaps can be shared across materials. Reference them to know when to release.
        // Frames of a vclip share the same mask, emissive and specular
        Array<Ref<Texture2D>, Count> Textures;
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

        void SetResourceHandles(Material& m) const {
            auto heapStartIndex = Render::Heaps->Materials.AllocateIndex();
            m.Handle = Render::Heaps->Materials.GetGpuHandle(heapStartIndex);

            for (int i = 0; i < Material::Count; i++) {
                if (!m.Textures[i]) m.Textures[i] = i == 0 ? Missing : Black;
                auto cpuHandle = Render::Heaps->Materials.GetCpuHandle(heapStartIndex + i);
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

        void LoadTextures(span<RuntimeTextureInfo> textures, bool reload = false);

        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(MaterialHandle h) {
            if (!Seq::inRange(_materials, (int)h)) return _defaultMaterial.Handle;
            return _materials[(int)h].Handle;
        }

    private:

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

        //void Free(string);

        // Resolves resource handle name into texture info ids
        // Used by level geometry
        int Resolve(const string& name);

        // Resolves a file name to a texture info id
        // Used by robots
        int ResolveFileName(string_view fileName);

        const RuntimeTextureInfo& GetTextureInfo(int handle) {
            if (Seq::inRange(_textures, handle)) {
                return _textures[handle];
            };

            return _defaultTexture;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetResource(const RuntimeTextureInfo& info, float time) {
            if (info.FrameHandles.empty())
                return _gpu.GetGpuHandle(info.BitmapHandle);

            return _gpu.GetGpuHandle(info.GetFrame(0, time));
        }

        D3D12_GPU_DESCRIPTOR_HANDLE GetResource(int handle, float time) {
            if (Seq::inRange(_textures, handle)) {
                auto& info = _textures[handle];
                return GetResource(info, time);
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
        // Allocs a slot for texture
        int AllocTextureInfo(RuntimeTextureInfo&& ti);
        int ResolveVClip(string_view frameName);
    };
};