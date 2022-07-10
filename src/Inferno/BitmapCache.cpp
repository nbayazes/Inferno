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

    //// Finds the entry for a texture based on name (not bitmap name)
    //MaterialHandle TextureCache::Find(string name) {
    //    for (int i = 0; i < _materials.size(); i++) {
    //        if (String::InvariantEquals(_materials[i].Name, name))
    //            return (MaterialHandle)i;
    //    }

    //    return MaterialHandle::None;
    //}

    //MaterialHandle TextureCache::Find(TexID id) {
    //    for (int i = 0; i < _materials.size(); i++) {
    //        if (_materials[i].PigID == id)
    //            return (MaterialHandle)i;
    //    }

    //    return MaterialHandle::None;
    //}

    //MaterialHandle TextureCache::Alloc(string name) {
    //    if (!name.empty()) {
    //        auto handle = Find(name);
    //        if (handle != MaterialHandle::None)
    //            return handle; // already allocated
    //    }

    //    auto handle = Alloc();
    //    if (!name.empty())
    //        Get(handle).Tablefile = 0; // hard code 0 for now

    //    return handle;
    //}

    //MaterialHandle TextureCache::Alloc(TexID id) {
    //    if (id == TexID::None) return MaterialHandle::None;

    //    auto handle = Find(id);
    //    if (handle != MaterialHandle::None)
    //        return handle; // already allocated

    //    return Alloc();
    //}

    //MaterialHandle TextureCache::Alloc() {
    //    // Find a free slot
    //    for (int i = 0; i < _materials.size(); i++) {
    //        if (!_materials[i].Used)
    //            return (MaterialHandle)i;
    //    }

    void TextureGpuCache::LoadDefaults() {
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
        Render::Heaps->Shader.GetFreeDescriptors();
    }

    //    // Alloc a new slot
    //    _materials.emplace_back();
    //    return (MaterialHandle)(_materials.size() - 1);
    //}
}
