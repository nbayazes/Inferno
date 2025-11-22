#pragma once

#include "Pig.h"
#include "Types.h"
#include <d3d12.h>
#include <DirectXTex.h>
#include <DirectXTex.inl>
#include <dxgiformat.h>
#include <span>
#include <strsafe.h>
#include <Windows.h>

namespace Inferno {
    class Image : public DirectX::ScratchImage {
    public:
        //uint16 width = 0;
        //uint16 height = 0;
        //DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        //std::vector<ubyte> data;
        Image Clone() const {
            Image image;
            if (SUCCEEDED(image.Initialize(GetMetadata())))
                memcpy(image.GetPixels(), GetPixels(), GetPixelsSize());

            return image;
        }

        //const DirectX::TexMetadata& GetMetadata() const {
        //    return _image.GetMetadata();
        //}

        //Image() {}

        //Image(const DirectX::Image& image) {
        //    data.resize(image.slicePitch);
        //    memcpy(data.data(), image.pixels, image.slicePitch);
        //    width = (uint16)image.width;
        //    height = (uint16)image.height;
        //}

        bool Empty() const {
            auto& metadata = GetMetadata();
            return metadata.width == 0 || metadata.height == 0;
        }

        span<uint8> GetPixels2() const {
            return { GetPixels(), GetPixelsSize() };
        }

        //const DirectX::Image* GetImage(size_t mip, size_t item, size_t slice) const {
        //    return _image.GetImage(mip, item, slice);
        //}

        bool GetPitch(size_t& rowPitch, size_t& slicePitch) const {
            auto& metadata = GetMetadata();
            return SUCCEEDED(DirectX::ComputePitch(metadata.format, metadata.width, metadata.height, rowPitch, slicePitch));
        }

        D3D12_RESOURCE_DESC GetResourceDesc() const {
            auto& metadata = GetMetadata();
            return CD3DX12_RESOURCE_DESC::Tex2D(metadata.format, metadata.width, (uint)metadata.height, (uint16)metadata.depth, (uint16)metadata.mipLevels);
            //return {
            //    .Dimension = metadata.dimension,
            //    .Alignment = metadata. ,
            //    .Width = metadata.width,
            //    .Height = metadata.height,
            //    .DepthOrArraySize = metadata. ,
            //    .MipLevels = ,
            //    .Format = ,
            //    .SampleDesc = ,
            //    .Layout = ,
            //    .Flags = 
            //}
        }

        D3D12_SUBRESOURCE_DATA GetSubresourceData() const {
            size_t rowPitch, slicePitch;
            auto& metadata = GetMetadata();
            if (!SUCCEEDED(DirectX::ComputePitch(metadata.format, metadata.width, metadata.height, rowPitch, slicePitch)))
                return {};

            return {
                .pData = GetPixels(),
                .RowPitch = (LONG_PTR)rowPitch,
                .SlicePitch = (LONG_PTR)slicePitch
            };
        }

        bool CopyToPigBitmap(PigBitmap& dest) const {
            const DirectX::ScratchImage* source = this;
            DirectX::ScratchImage decompressed;
            auto& metadata = GetMetadata();

            if (DirectX::IsCompressed(metadata.format)) {
                if (FAILED(Decompress(*GetImage(0, 0, 0), metadata.format, decompressed)))
                    //if (FAILED(Decompress(*_buffer.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, decompressed)))
                    return false;

                source = &decompressed;
            }

            dest.Data.resize(source->GetPixelsSize() / 4); // pig bitmap stores 4 bytes per pixel
            memcpy(dest.Data.data(), source->GetPixels(), source->GetPixelsSize());
            dest.Info.Width = (uint16)metadata.width;
            dest.Info.Height = (uint16)metadata.height;
            return true;
        }

        bool GenerateMipmaps(bool wrapu = true, bool wrapv = true) {
            using namespace DirectX;
            auto flags = TEX_FILTER_DEFAULT;
            if (wrapu) flags |= TEX_FILTER_WRAP_U;
            if (wrapv) flags |= TEX_FILTER_WRAP_V;

            // copy
            ScratchImage source;
            if (!SUCCEEDED(source.InitializeFromImage(*GetImage(0, 0, 0))))
                return false;

            auto image = source.GetImage(0, 0, 0);
            size_t levels = 0; // 0 generates all levels
            return SUCCEEDED(DirectX::GenerateMipMaps(*image, flags, levels, *this));
        }

        bool Resize(bool wrapu, bool wrapv, uint8 width, uint8 height) {
            using namespace DirectX;
            auto flags = TEX_FILTER_DEFAULT;
            if (wrapu) flags |= TEX_FILTER_WRAP_U;
            if (wrapv) flags |= TEX_FILTER_WRAP_V;

            ScratchImage source;
            if (!SUCCEEDED(source.InitializeFromImage(*GetImage(0, 0, 0))))
                return false;
            //source.Initialize2D(metadata.format, metadata.width, metadata.height, 1, 1);
            //memcpy(source.GetPixels(), _buffer.GetPixels(), _buffer.GetPixelsSize());

            auto srcImage = source.GetImage(0, 0, 0);

            // replace the buffer
            if (!SUCCEEDED(DirectX::Resize(*srcImage, width, height, flags, *this)))
                return false;

            return true;
        }

        bool LoadPigBitmap(const PigBitmap& texture) {
            size_t rowPitch, slicePitch;
            if (FAILED(DirectX::ComputePitch(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, texture.Info.Width, texture.Info.Height, rowPitch, slicePitch)))
                return false;

            DirectX::Image image(texture.Info.Width, texture.Info.Height,
                                 DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, rowPitch,
                                 slicePitch, (uint8*)texture.Data.data());

            if (FAILED(InitializeFromImage(image)))
                return false;

            return true;
            //_buffer.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, texture.Info.Width, texture.Info.Height, 
        }

        // SRGB indicates whether to treat the source image as SRGB or linear
        bool LoadWIC(span<ubyte> source, bool srgb) {
            using namespace DirectX;
            //size_t rowPitch, slicePitch;

            auto flags = srgb ? WIC_FLAGS_DEFAULT_SRGB : WIC_FLAGS_FORCE_LINEAR;

            ScratchImage result, premultiplied;
            if (FAILED(LoadFromWICMemory(source.data(), source.size(), flags, nullptr, result)))
                return false;

            auto image = result.GetImage(0, 0, 0);

            /*if (FAILED(ComputePitch(metadata.format, metadata.width, metadata.height, rowPitch, slicePitch)))
                return false;

            DirectX::Image image(metadata.width, metadata.height, format, rowPitch, slicePitch, result.GetPixels());*/
            return SUCCEEDED(PremultiplyAlpha(*image, DirectX::TEX_PMALPHA_DEFAULT, *this));
        }

        bool LoadTGA(span<ubyte> source, bool srgb) {
            using namespace DirectX;

            auto flags = srgb ? TGA_FLAGS_DEFAULT_SRGB : TGA_FLAGS_FORCE_LINEAR;

            ScratchImage result, premultiplied;
            if (FAILED(LoadFromTGAMemory(source.data(), source.size(), flags, nullptr, result)))
                return false;

            auto image = result.GetImage(0, 0, 0);
            return SUCCEEDED(PremultiplyAlpha(*image, DirectX::TEX_PMALPHA_DEFAULT, *this));
        }

        bool LoadDDS(span<ubyte> source, bool srgb) {
            using namespace DirectX;
            ScratchImage dds, decompressed;

            if (FAILED(LoadFromDDSMemory(source.data(), source.size(), DDS_FLAGS_NONE, nullptr, *this)))
                return false;

            auto& metadata = GetMetadata();
            if (srgb) OverrideFormat(MakeSRGB(metadata.format));

            //if(metadata.miscFlags
            //if (FAILED(Decompress(*dds.GetImage(0, 0, 0), metadata.format, _buffer)))
            //    return false;

            return true;
            //auto image = decompressed.GetImage(0, 0, 0);
            //return SUCCEEDED(_buffer.InitializeFromImage(*image));
        }
    };
}
