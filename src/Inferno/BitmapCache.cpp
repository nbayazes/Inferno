#include "pch.h"
#include "BitmapCache.h"
#include "Resources.h"
#include "Graphics/Render.h"

namespace Inferno {
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
    }

    void TextureGpuCache::LoadDefaults() {
        auto batch = BeginUpload();
        White = MakeRef<Texture2D>();
        Black = MakeRef<Texture2D>();
        Missing = MakeRef<Texture2D>();

        List<ubyte> bmp(64 * 64 * 4);
        FillTexture(bmp, 0, 0, 0, 255);
        Black->Load(batch, bmp.data(), 64, 64, "black");

        FillTexture(bmp, 255, 255, 255, 255);
        White->Load(batch, bmp.data(), 64, 64, "white");

        FillTexture(bmp, 255, 0, 255, 255);
        Missing->Load(batch, bmp.data(), 64, 64, "purple");

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

            for (uint i = 0; i < Material::Count; i++) {
                auto handle = Render::Heaps->Reserved.Allocate();
                if (i == 0)
                    _whiteMaterial.Handle = handle.GetGpuHandle();

                if (i == 0)
                    White->CreateShaderResourceView(handle.GetCpuHandle());
                else
                    Black->CreateShaderResourceView(handle.GetCpuHandle());
            }
        }


        EndUpload(batch);
    }

    void TextureGpuCache::LoadTextures(span<RuntimeTextureInfo> textures, bool reload) {
        bool loaded = true;
        for (auto& texture : textures) {
            if (!_textures.contains(texture.FileName)) {
                loaded = false;
                break;
            }
        }

        if (loaded) return;

        auto batch = BeginUpload();

        for (auto& tex : textures) {
            if (tex.VClip >= 0) {
                // Load each frame in the animation
                auto& vclip = Resources::VClips[tex.VClip];
                if (tex.FrameHandles.size() != vclip.Frames.size())
                    tex.FrameHandles.resize(vclip.Frames.size(), MaterialHandle::None);

                for (int i = 0; i < tex.FrameHandles.size(); i++) {
                    auto& handle = tex.FrameHandles[i];
                    if (handle == MaterialHandle::None || reload)
                        Load(batch, handle, vclip.Frames[i]);
                }
            }
            else if (tex.BitmapHandle == MaterialHandle::None || reload) {
                if (auto bmp = Resources::ReadOutrageBitmap(tex.FileName)) {
                    Load(batch, tex.BitmapHandle, *bmp);
                }
            }
        }

        EndUpload(batch);

        Render::Adapter->PrintMemoryUsage();
        Render::Heaps->Materials.GetFreeDescriptors();
    }

    int TextureCache::Resolve(const string& name) {
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

    int TextureCache::ResolveFileName(string_view fileName) {
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

    int TextureCache::AllocTextureInfo(RuntimeTextureInfo&& ti) {
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

        if (ti.Animated()) {
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

    int TextureCache::ResolveVClip(string_view frameName) {
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

    //    // Alloc a new slot
    //    _materials.emplace_back();
    //    return (MaterialHandle)(_materials.size() - 1);
    //}
}
