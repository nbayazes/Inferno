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

    //    // Alloc a new slot
    //    _materials.emplace_back();
    //    return (MaterialHandle)(_materials.size() - 1);
    //}
}
