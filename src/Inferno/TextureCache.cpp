#include "pch.h"
#include "TextureCache.h"
#include "BitmapTable.h"
#include "HamFile.h"
#include "HogFile.h"
#include "NormalMap.h"
#include "Resources.h"
#include "Streams.h"

namespace Inferno {
    namespace {
        constexpr uint32 CACHE_SIG = MakeFourCC("CHCE");
        constexpr uint32 CACHE_VERSION = 2;
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

    void Downsample(span<uint8> src, uint srcWidth, uint srcHeight, span<uint8> dest, uint destWidth, uint destHeight) {
        auto sample = [&](uint x, uint y) { return src[(y % srcHeight) * srcWidth + x % srcWidth]; };

        // bilinear
        //for (uint y = 0; y < destHeight; y++) {
        //    for (uint x = 0; x < destWidth; x++) {
        //        uint16 s0 = sample(x * 2, y * 2);
        //        uint16 s1 = sample(x * 2 + 1, y * 2);
        //        uint16 s2 = sample(x * 2, y * 2 + 1);
        //        uint16 s3 = sample(x * 2 + 1, y * 2 + 1);

        //        dest[y * destWidth + x] = uint8((s0 + s1 + s2 + s3) / 4);
        //    }
        //}

        // point sample
        for (uint y = 0; y < destHeight; y++) {
            for (uint x = 0; x < destWidth; x++) {
                dest[y * destWidth + x] = sample(x * 2, y * 2);
            }
        }
    }

    void Downsample(span<Palette::Color> src, uint srcWidth, uint srcHeight, span<Palette::Color> dest, uint destWidth, uint destHeight) {
        auto sample = [&](uint x, uint y) { return src[(y % srcHeight) * srcWidth + x % srcWidth]; };

        // point sample
        for (uint y = 0; y < destHeight; y++) {
            for (uint x = 0; x < destWidth; x++) {
                dest[y * destWidth + x] = sample(x * 2, y * 2);
            }
        }
    }

    // Returns the number of mip levels
    template <class T>
    uint8 GenerateMipmaps(List<T>& bitmap, uint srcWidth, uint srcHeight) {
        bitmap.reserve(bitmap.size() * 3 / 2); // mipmaps take roughly 33% more space

        uint offset = 0;
        auto begin = bitmap.begin();
        uint8 mips = 1;

        for (int i = 0; i < 7; i++) {
            auto destWidth = srcWidth / 2;
            auto destHeight = srcHeight / 2;
            auto srcSize = srcWidth * srcHeight;
            auto destSize = destWidth * destHeight;

            bitmap.resize(bitmap.size() + destSize);

            auto srcBegin = bitmap.begin() + offset;
            auto srcEnd = bitmap.begin() + offset + srcWidth * srcHeight;

            span src = { begin, begin + srcSize };
            span dest = { begin + srcSize, begin + srcSize + destSize };

            Downsample(src, srcWidth, srcHeight, dest, destWidth, destHeight);

            srcWidth /= 2;
            srcHeight /= 2;
            offset += srcWidth * srcHeight;
            begin += srcSize;
            mips++;

            if (srcWidth == 1 || srcHeight == 1)
                break; // min level reached
        }

        return mips;
    }

    void Serialize(StreamWriter& stream, span<TextureMapCache::Entry> entries) {
        stream.WriteUInt32(CACHE_SIG);
        stream.WriteUInt32(CACHE_VERSION);
        stream.WriteInt32((int32)entries.size());

        auto headerStart = stream.Position();

        const auto writeHeader = [&stream](TextureMapCache::Entry& entry) {
            stream.WriteInt16((int16)entry.Id);
            stream.WriteUInt64(entry.DataOffset);
            stream.WriteUInt16(entry.Width);
            stream.WriteUInt16(entry.Height);
            stream.WriteUInt8(entry.Mips);

            // Write data lengths
            stream.WriteUInt32((uint32)entry.Diffuse.size() * sizeof(Palette::Color));
            stream.WriteUInt32((uint32)entry.Specular.size());
            stream.WriteUInt32((uint32)entry.Normal.size() * sizeof(Palette::Color));
            stream.WriteUInt32((uint32)entry.Mask.size());
        };

        Seq::iter(entries, writeHeader);

        auto dataStart = stream.Position();

        // Write image data
        for (auto& entry : entries) {
            entry.DataOffset = stream.Position() - dataStart;

            stream.WriteBytes({ (ubyte*)entry.Diffuse.data(), entry.Diffuse.size() * sizeof(Palette::Color) });
            stream.WriteBytes(entry.Specular);
            stream.WriteBytes({ (ubyte*)entry.Normal.data(), entry.Normal.size() * sizeof(Palette::Color) });
            stream.WriteBytes(entry.Mask);
        }

        // Write headers again with the updated data offsets
        stream.Seek(headerStart);
        Seq::iter(entries, writeHeader);
    }

    void TextureMapCache::Deserialize(StreamReader& stream) {
        auto sig = stream.ReadUInt32();
        if (sig != CACHE_SIG)
            throw Exception("Unknown cache file header");

        auto version = stream.ReadUInt32();
        if (version != CACHE_VERSION)
            throw Exception("Old cache file version");

        auto size = stream.ReadElementCount();

        uint texturesRead = 0;

        for (size_t i = 0; i < size; i++) {
            auto id = stream.ReadInt16();
            ASSERT(id < Entries.size());
            if (!Seq::inRange(Entries, id))
                throw Exception(fmt::format("Cache entry id {} larger than capacity {}", id, _size));

            auto& entry = Entries[id];
            entry.Id = (TexID)id;
            entry.DataOffset = stream.ReadUInt64();
            entry.Width = stream.ReadUInt16();
            entry.Height = stream.ReadUInt16();
            entry.Mips = stream.ReadByte();

            // Read data lengths
            entry.DiffuseLength = stream.ReadUInt32();
            entry.SpecularLength = stream.ReadUInt32();
            entry.NormalLength = stream.ReadUInt32();
            entry.MaskLength = stream.ReadUInt32();

            texturesRead++;
        }

        _dataStart = stream.Position();

        //for (auto& entry : Entries) {
        //    if (entry.DiffuseLength || entry.NormalLength || entry.SpecularLength || entry.MaskLength) {
        //        // Read data
        //        stream.Seek(_dataStart + entry.DataOffset);
        //        entry.Diffuse = stream.ReadStructs<Palette::Color>(entry.DiffuseLength / sizeof Palette::Color);
        //        entry.Specular = stream.ReadUBytes(entry.SpecularLength);
        //        entry.Normal = stream.ReadStructs<Palette::Color>(entry.NormalLength / sizeof Palette::Color);
        //        entry.Mask = stream.ReadUBytes(entry.MaskLength);
        //    }
        //}

        SPDLOG_INFO("Read {} textures from cache", texturesRead);
    }

    bool IsLevelTexture(const HamFile& ham, TexID id, bool isD1) {
        auto tex255 = isD1 ? TexID(971) : TexID(1485);
        //auto tid = Resources::LookupLevelTexID(id);
        auto tid = Seq::tryItem(ham.LevelTexIdx, (int)id);
        if (!tid) return false;

        if (*tid != LevelTexID(255) || id == tex255) return true;

        // Check if any wall clips contain this ID
        for (auto& effect : ham.Effects) {
            for (auto& frame : effect.VClip.GetFrames()) {
                if (frame == id) return true;
            }
        }

        // Default tid is 255, so check if the real 255 texid is passed in
        return *tid != LevelTexID(255) || id == tex255;
    }

    void TextureMapCache::GenerateTextures(const HamFile& ham, const PigFile& pig, const Palette& palette) {
        auto bitmaps = ReadAllBitmaps(pig, palette);

        Set<TexID> vclips;
        for (auto& vclip : ham.VClips) {
            Seq::insert(vclips, vclip.GetFrames());
        }

        uint levelCount = 0;
        uint objectCount = 0;

        for (size_t i = 1; i < bitmaps.size(); i++) {
            auto tid = TexID(i);

            bool isLevelTexture = IsLevelTexture(ham, tid, true);
            bool isObjectTexture = Seq::contains(ham.ObjectBitmaps, tid);
            bool isVClip = vclips.contains(tid);

            // skip hud textures
            if (!isLevelTexture && !isObjectTexture && !isVClip) continue;

            if (isLevelTexture) levelCount++;
            if (isObjectTexture) objectCount++;

            auto& entry = Entries.emplace_back();
            entry.Id = tid;
            entry.Width = pig.Entries[i].Width;
            entry.Height = pig.Entries[i].Height;

            // Generate diffuse mipmaps
            entry.Diffuse = bitmaps[i].Data;
            entry.Mips = GenerateMipmaps(entry.Diffuse, entry.Width, entry.Height);

            if (isLevelTexture || isObjectTexture) {
                // Only generate specular and normal maps for level and 3D object textures
                entry.Specular = CreateSpecularMap(bitmaps[i]);
                entry.Normal = CreateNormalMap(bitmaps[i]);

                GenerateMipmaps(entry.Specular, entry.Width, entry.Height);
                GenerateMipmaps(entry.Normal, entry.Width, entry.Height);
            }

            // Add transparency mask
            if (isLevelTexture && pig.Entries[i].SuperTransparent) {
                entry.Mask = bitmaps[i].Mask; // copy the mask
                ExpandMask(bitmaps[i].Info, entry.Mask);
                GenerateMipmaps(entry.Mask, entry.Width, entry.Height);
            }
        }

        SPDLOG_INFO("Cached {} level bitmaps, {} object bitmaps, and {} vclips", levelCount, objectCount, vclips.size());
    }

    TextureMapCache::TextureMapCache(filesystem::path path, uint size): _size(size), Path(std::move(path)) {
        SPDLOG_INFO("Reading texture cache {}", Path.string());
        _stream = make_unique<StreamReader>(Path);
        Entries.resize(_size);
        Deserialize(*_stream);
    }

    void TextureMapCache::Write(const std::filesystem::path& path) {
        try {
            if (Entries.empty()) {
                SPDLOG_WARN("Tried to write an empty texture cache file");
                return;
            }

            StreamWriter stream(path);
            Serialize(stream, Entries);
        }
        catch (const Exception& e) {
            SPDLOG_ERROR("Texture cache write error: {}", e.what());
        }
    }

    bool CacheFileIsValid(const filesystem::path& path) {
        try {
            if (!std::filesystem::exists(path)) return false;

            StreamReader stream(path);

            auto sig = stream.ReadUInt32();
            if (sig != CACHE_SIG) return false;

            auto version = stream.ReadUInt32();
            if (version != CACHE_VERSION) return false;
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool WriteTextureCache(const HamFile& ham, const PigFile& pig, const Palette& palette, const filesystem::path& destination) {
        if (CacheFileIsValid(destination)) {
            SPDLOG_INFO("{} already exists", destination.string());
            return true;
        }

        TextureMapCache cache;
        SPDLOG_INFO("Generating D1 texture cache");
        cache.GenerateTextures(ham, pig, palette);
        SPDLOG_INFO("Writing {} textures to cache {}", cache.Entries.size(), destination.string());
        cache.Write(destination);
        return true;
    }

    void LoadTextureCaches() {
        if (filesystem::exists(D1_CACHE) && D1TextureCache.Entries.empty())
            D1TextureCache = TextureMapCache(D1_CACHE, 1800);

        if (filesystem::exists(D2_CACHE) && D2TextureCache.Entries.empty())
            D2TextureCache = TextureMapCache(D2_CACHE, 2700);

        if (filesystem::exists(D1_DEMO_CACHE) && D1TextureCache.Entries.empty())
            D1DemoTextureCache = TextureMapCache(D1_DEMO_CACHE, 1800);
    }
}
