#include "pch.h"
#include "TextureAllocator.h"
#include <execution>
#include "Render.h"
#include "GpuResources.h"
#include "NormalMap.h"
#include "Resources.h"
#include "ScopedTimer.h"
#include "unordered_dense.h"

namespace Inferno::textures {
    namespace {
        struct Upload {
            //enum Flags : uint8 { None, GenerateMips = 1 };

            string name;
            Image image;
            //Flags flags = None;
        };

        struct TextureResource {
            ComPtr<D3D12MA::Allocation> allocation; // free with the resource
            ComPtr<ID3D12Resource> intermediate; // clear after uploading
            GpuResource resource;
        };

        ankerl::unordered_dense::map<string, TextureResource, StringHash, StringEquals> _textures;
        std::vector<Upload> _queue;
        Ptr<CommandQueue> _copyQueue;
        Ptr<CommandContext> _copyContext;
        int64 _elapsedRead = 0;

        bool _uploading = false;
    }

    void Init(ID3D12Device* device) {
        _copyQueue = make_unique<CommandQueue>(device, D3D12_COMMAND_LIST_TYPE_COPY, "Texture Upload Queue");
        _copyContext = MakePtr<CommandContext>(device, _copyQueue.get(), "Texture Upload Context");
    }

    void Shutdown() {
        _textures.clear();
        _copyContext.reset();
        _copyQueue.reset();
    }

    GpuResource* Find(string_view name) {
        if (auto find = _textures.find(name); find != _textures.end()) {
            return &find->second.resource;
        }

        return nullptr;
    }

    void BeginUpload() {
        if (!_copyContext) {
            Init(Render::Device);
        }

        if (_uploading) {
            __debugbreak();
            return;
        }

        _uploading = true;
    }

    std::mutex _uploadMutex;

    void QueueUpload(string_view name, Image&& image) {
        std::scoped_lock lock(_uploadMutex);

        if (!_uploading) {
            __debugbreak();
            return;
        }

        if (image.Empty()) {
            __debugbreak();
            return;
        }

        _queue.push_back({
            .name = string(name),
            .image = std::move(image),
        });
    }

    struct MaterialLoadInfo {
        string name;
        //Image diffuse;
        //Image specular;
        //Image normal;
        //Image emissive;
        //Image mask;
        bool wrapu = true;
        bool wrapv = true;
    };

    List<MaterialLoadInfo> _materialLoadQueue;
    std::atomic<uint64> genMips, genSpecularCount, genNormalCount;

    // Generates special maps for a material and queues their upload
    void GenerateMaterial(const MaterialLoadInfo& mat) {
        ASSERT(!mat.name.empty());

        auto diffuse = Resources::ReadImage(mat.name, true);
        if (!diffuse) return; // Diffuse is required
        auto specular = Resources::ReadImage(mat.name + "_s", false);
        auto normal = Resources::ReadImage(mat.name + "_n", false);
        auto emissive = Resources::ReadImage(mat.name + "_e", false);
        auto mask = Resources::ReadImage(mat.name + "_st", false);

        if (!normal || !specular) {
            PigBitmap diffuseBitmap;
            diffuse->CopyToPigBitmap(diffuseBitmap);
            auto& metadata = diffuse->GetMetadata();

            if (!normal) {
                auto normalMap = CreateNormalMap(diffuseBitmap);
                normal = Image();
                normal->Load<Palette::Color>(normalMap, metadata.width, metadata.height, DXGI_FORMAT_R8G8B8A8_UNORM);
                genNormalCount++;
            }

            if (!specular) {
                auto specularMap = CreateSpecularMap(diffuseBitmap);
                specular = Image();
                specular->Load<uint8>(specularMap, metadata.width, metadata.height, DXGI_FORMAT_R8_UNORM);
                genSpecularCount++;
            }
        }

        auto queueUpload = [](Option<Image>& image, string_view name) {
            if (image) {
                if (!image->HasMipmaps()) {
                    image->GenerateMipmaps(); // always want mipmaps
                    genMips++;
                }
                QueueUpload(name, std::move(*image));
            }
        };

        queueUpload(diffuse, mat.name);
        queueUpload(emissive, mat.name + "_e");
        queueUpload(specular, mat.name + "_s");
        queueUpload(mask, mat.name + "_st");
        queueUpload(normal, mat.name + "_n");

        //if (specular && !specular->HasMipmaps()) {
        //    specular->GenerateMipmaps();
        //    genMips++;
        //}
        //QueueUpload(mat.name + "_s", std::move(*specular));

        //if (normal) {
        //    if (!normal->HasMipmaps()) {
        //        normal->GenerateMipmaps();
        //        genMips++;
        //    }
        //    QueueUpload(mat.name + "_n", std::move(*normal));
        //}

        //if (emissive) {
        //    if (!emissive->HasMipmaps()) {
        //        emissive->GenerateMipmaps();
        //        genMips++;
        //    }
        //    QueueUpload(mat.name + "_e", std::move(*emissive));
        //}

        //if (mask) {
        //    if (!mask->HasMipmaps()) {
        //        mask->GenerateMipmaps();
        //        genMips++;
        //    }
        //    QueueUpload(mat.name + "_st", std::move(*mask));
        //}

        //if (!diffuse->HasMipmaps()) {
        //    diffuse->GenerateMipmaps();
        //    genMips++;
        //}

        //QueueUpload(mat.name, std::move(*diffuse));
    }

    UploadStats EndUpload() {
        UploadStats stats;
        genMips = genSpecularCount = genNormalCount = 0;

        {
            ScopedTimer timer(stats.elapsedGen);

            std::for_each(std::execution::par, _materialLoadQueue.begin(), _materialLoadQueue.end(), [](const MaterialLoadInfo& mat) {
                GenerateMaterial(mat);
            });
        }

        stats.genMips = (uint)genMips;
        stats.genNormals = (uint)genNormalCount;
        stats.genSpecular = (uint)genSpecularCount;

        {
            ScopedTimer timer(stats.elapsedUpload);

            std::vector<ComPtr<ID3D12Resource>> intermediates;
            std::vector<ComPtr<D3D12MA::Allocation>> allocations;

            // reset start of upload
            _copyContext->Reset();

            // try allocating an entire block of memory and placing resources into it
            //{
            //    uint64 totalSize = 0;

            //    for (auto& item : _queue) {
            //        auto resourceDesc = item.image.GetResourceDesc();

            //        UINT64 uploadBufferSize;
            //        Render::Device->GetCopyableFootprints(&resourceDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);
            //        totalSize += uploadBufferSize;
            //        //auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
            //    }

            //    D3D12MA::ALLOCATION_DESC memoryDesc{
            //        .HeapType = D3D12_HEAP_TYPE_UPLOAD,
            //        .ExtraHeapFlags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES
            //    };

            //    D3D12_RESOURCE_ALLOCATION_INFO memoryAlloc{
            //        .SizeInBytes = AlignTo(totalSize, 64 * 1024), // must be 64 kb aligned
            //        .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT
            //    };

            //    ComPtr<D3D12MA::Allocation> memory;
            //    ThrowIfFailed(Render::Allocator->AllocateMemory(&memoryDesc, &memoryAlloc, &memory));
            //    
            //    uint64 offset = 0;
            //    for (auto& item : _queue) {
            //        // todo: textures might not be 2D (cubemaps)
            //        auto& intermediate = intermediates.emplace_back();

            //        //D3D12MA::ALLOCATION_DESC heapDesc{ .HeapType = D3D12_HEAP_TYPE_UPLOAD };
            //        auto resourceDesc = item.image.GetResourceDesc();
            //        //auto& allocation = allocations.emplace_back();

            //        UINT64 uploadBufferSize;
            //        Render::Device->GetCopyableFootprints(&resourceDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);
            //        //auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

            //        ThrowIfFailed(Render::Allocator->CreateAliasingResource(
            //            memory.Get(), 
            //            offset,
            //            &resourceDesc, 
            //            D3D12_RESOURCE_STATE_GENERIC_READ,
            //            nullptr, 
            //            IID_PPV_ARGS(&intermediate)
            //        ));

            //        offset += uploadBufferSize;

            //        Texture2D resource;
            //        resource.Load(_copyContext->GetCommandList(), intermediate.Get(), item.image, item.name);
            //        _textures[item.name] = std::move(resource);
            //    }
            //}

            std::mutex cmdListMutex;

            std::for_each(std::execution::par, _queue.begin(), _queue.end(), [&cmdListMutex](const Upload& upload) {
                // todo: textures might not be 2D (cubemaps)
                TextureResource texture;

                D3D12MA::ALLOCATION_DESC heapDesc{
                    .Flags = D3D12MA::ALLOCATION_FLAG_STRATEGY_MIN_TIME, // prefer the fastest allocation, not the best fit
                    .HeapType = D3D12_HEAP_TYPE_UPLOAD,
                };

                auto resourceDesc = upload.image.GetResourceDesc();

                UINT64 uploadBufferSize;
                Render::Device->GetCopyableFootprints(&resourceDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);
                auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

                ThrowIfFailed(Render::Allocator->CreateResource(
                    &heapDesc,
                    &uploadBufferDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    &texture.allocation,
                    IID_PPV_ARGS(&texture.intermediate)
                ));

                Texture2D resource;
                {
                    std::scoped_lock lock(cmdListMutex); // dx12 command lists and the texture map are not thread safe
                    resource.Load(_copyContext->GetCommandList(), texture.intermediate.Get(), upload.image, upload.name);
                    texture.resource = std::move(resource);
                    _textures[upload.name] = std::move(texture);
                }
            });

            _copyContext->Execute();
            _copyContext->WaitForIdle();
        }

        //stats.elapsedUpload = elapsedTime;
        stats.elapsedRead = _elapsedRead;
        stats.uploads = (uint)_queue.size();

        //SPDLOG_INFO("uploaded {} textures in {}ms", _queue.size(), Clock.GetTotalMilliseconds() - start);
        _queue.clear();
        _uploading = false;
        _elapsedRead = 0;
        _materialLoadQueue.clear();
        return stats;
    }


    void QueueLoadMaterial(string_view name) {
        ScopedTimer timer(_elapsedRead);
        if (name.empty()) return;

        auto diffuse = Resources::ReadImage(name, true);
        if (!diffuse) return; // no diffuse texture

        auto& info = _materialLoadQueue.emplace_back();
        info.name = name;
        //info.diffuse = std::move(*diffuse);
        //if (specular) info.specular = std::move(*specular);
        //if (normal) info.normal = std::move(*normal);
        //if (emissive) info.emissive = std::move(*emissive);
        //if (mask) info.mask = std::move(*mask);

        if (auto materialInfo = Resources::TryGetMaterial(name)) {
            if (!HasFlag(materialInfo->Flags, MaterialFlags::WrapU)) info.wrapu = false;
            if (!HasFlag(materialInfo->Flags, MaterialFlags::WrapV)) info.wrapv = false;
        }
    }
}
