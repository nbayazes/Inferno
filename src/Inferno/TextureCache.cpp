#include "pch.h"
#include "TextureCache.h"
#include "FileSystem.h"
#include "HamFile.h"
#include "HogFile.h"
#include "NormalMap.h"
#include "Streams.h"

namespace Inferno {
    namespace {
        constexpr uint32 CACHE_SIG = MakeFourCC("CHCE");
        constexpr uint32 CACHE_VERSION = 4;
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
    template<class T>
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

    void Serialize(StreamWriter& stream, const TextureMapCache& cache) {
        stream.Write(CACHE_SIG);
        stream.Write(CACHE_VERSION);
        //auto pos = stream.Position();
        stream.Write((int32)cache.Entries.size());

        for (auto& entry : cache.Entries) {
            stream.Write((int16)entry.Id);
            stream.Write((uint16)entry.Width);
            stream.Write((uint16)entry.Height);
            stream.Write((uint8)entry.Mips);

            const auto diffuseSize = entry.Diffuse.size() * sizeof Palette::Color;
            const auto normalSize = entry.Normal.size() * sizeof Palette::Color;

            // Write data lengths
            stream.Write((uint32)diffuseSize);
            stream.Write((uint32)entry.Specular.size());
            stream.Write((uint32)normalSize);
            stream.Write((uint32)entry.Mask.size());

            // write image data
            stream.WriteBytes({ (ubyte*)entry.Diffuse.data(), diffuseSize });
            stream.WriteBytes(entry.Specular);
            stream.WriteBytes({ (ubyte*)entry.Normal.data(), normalSize });
            stream.WriteBytes(entry.Mask);
        }
    }

    bool CacheFileIsValid(string_view path) {
        try {
            if(!std::filesystem::exists(path)) return false;

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

    TextureMapCache Deserialize(StreamReader& stream) {
        auto sig = stream.ReadUInt32();
        if (sig != CACHE_SIG)
            throw Exception("Unknown cache file header");

        auto version = stream.ReadUInt32();
        if (version != CACHE_VERSION)
            throw Exception("Old cache file version");

        auto size = stream.ReadElementCount();

        TextureMapCache cache;
        cache.Entries.resize(3000);

        for (size_t i = 0; i < size; i++) {
            auto id = stream.ReadInt16();
            ASSERT(id < cache.Entries.size());
            if(!Seq::inRange(cache.Entries, id))
                throw Exception("Cache entry id out of range");

            auto& entry = cache.Entries[id];
            entry.Id = (TexID)id;
            entry.Width = stream.ReadUInt16();
            entry.Height = stream.ReadUInt16();
            entry.Mips = stream.ReadByte();

            // Read data lengths
            entry.DiffuseLength = stream.ReadUInt32();
            entry.SpecularLength = stream.ReadUInt32();
            entry.NormalLength = stream.ReadUInt32();
            entry.MaskLength = stream.ReadUInt32();

            // Read data
            {
                //entry.Diffuse = stream.ReadStructs<Palette::Color>(entry.DiffuseLength / sizeof(Palette::Color));

                auto bytes = stream.ReadUBytes(entry.DiffuseLength);
                span diffuse{ (Palette::Color*)bytes.data(), bytes.size() / sizeof Palette::Color };
                entry.Diffuse = List<Palette::Color>(diffuse.begin(), diffuse.end());
                //entry.Diffuse = List<Palette::Color>((Palette::Color*)bytes.data(), (Palette::Color*)bytes.data() + bytes.size() / sizeof Palette::Color);
            }

            entry.Specular = stream.ReadUBytes(entry.SpecularLength);

            {
                auto normalBytes = stream.ReadUBytes(entry.NormalLength);
                span normal{ (Palette::Color*)normalBytes.data(), normalBytes.size() / sizeof Palette::Color };
                entry.Normal = List<Palette::Color>(normal.begin(), normal.end());
            }

            entry.Mask = stream.ReadUBytes(entry.MaskLength);
        }

        return cache;
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
            if(!isLevelTexture && !isObjectTexture && !isVClip) continue;

            if(isLevelTexture) levelCount++;
            if(isObjectTexture) objectCount++;

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

                //entry.Specular.reserve(entry.Specular.size() * 3 / 2);

                //for (size_t size = 64; size > 1; size /= 2) {
                //    auto start = entry.Specular.end();
                //    entry.Specular.resize(entry.Specular.size() + size * 2);

                //    std::fill(start, entry.Specular.end(), entry.Mips * 30);
                //}
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

    TextureMapCache TextureMapCache::Read(const filesystem::path& path) {
        try {
            StreamReader stream(path);
            auto cache = Deserialize(stream);
            cache.Path = path;

            //for (auto& entry : cache.Entries) {
                //entry.Mips = 1;

                //if (entry.Width == 64 && entry.Height == 64) {
                    //entry.Mips = GenerateMipmaps(entry.Specular, entry.Width, entry.Height);

                    //entry.Specular.reserve(entry.Specular.size() * 3 / 2);


                    //auto srcWidth = entry.Width;
                    //auto srcHeight = entry.Height;
                    //uint offset = 0;
                    //auto begin = entry.Specular.begin();

                    //for (int i = 0; i < 7; i++) {
                    //    auto destWidth = srcWidth / 2;
                    //    auto destHeight = srcHeight / 2;
                    //    auto srcSize = srcWidth * srcHeight;
                    //    auto destSize = destWidth * destHeight;

                    //    entry.Specular.resize(entry.Specular.size() + destSize);

                    //    auto srcBegin = entry.Specular.begin() + offset;
                    //    auto srcEnd = entry.Specular.begin() + offset + srcWidth * srcHeight;

                    //    span src = { begin, begin + srcSize };
                    //    span dest = { begin + srcSize, begin + srcSize + destSize };

                    //    Downsample(src, srcWidth, srcHeight, dest, destWidth, destHeight);

                    //    srcWidth /= 2;
                    //    srcHeight /= 2;
                    //    offset += srcWidth * srcHeight;
                    //    entry.Mips++;
                    //    begin += srcSize;
                    //    if(srcWidth == 1 || srcHeight == 1) break;
                    //}
                //}
            //}

            return cache;
        }
        catch (const Exception& e) {
            SPDLOG_ERROR("Texture cache read error: {}", e.what());
            return {};
        }
    }

    void TextureMapCache::Write(const std::filesystem::path& path) const {
        try {
            if (Entries.empty()) {
                SPDLOG_WARN("Tried to write an empty texture cache file");
                return;
            }

            //std::ofstream file(path, std::ios::binary);
            StreamWriter stream(path);
            Serialize(stream, *this);
        }
        catch (const Exception& e) {
            SPDLOG_ERROR("Texture cache write error: {}", e.what());
        }
    }

    //void BuildGameTextureCache(filesystem::path& path, HogFile& hog) {


    //}

    constexpr auto D1_CACHE = "cache/d1.cache";

    void BuildTextureMapCache() {
        if (CacheFileIsValid(D1_CACHE)) {
            SPDLOG_INFO("{} already exists", D1_CACHE);
            return;
        }

        if (!filesystem::exists("d1/descent.hog")) {
            SPDLOG_WARN("d1/descent.hog is missing");
            return;
        }

        if (!filesystem::exists("d1/descent.pig")) {
            SPDLOG_WARN("d1/descent.pig is missing");
            return;
        }

        auto hog = HogFile::Read("d1/descent.hog");
        auto paletteData = hog.ReadEntry("palette.256");
        auto palette = ReadPalette(paletteData);
        auto pigData = File::ReadAllBytes("d1/descent.pig");

        HamFile ham;
        PigFile pig;
        SoundFile sounds;
        ReadDescent1GameData(pigData, palette, ham, pig, sounds);
        pig.Path = "d1/descent.pig";

        TextureMapCache cache;
        SPDLOG_INFO("Generating D1 texture cache");
        cache.GenerateTextures(ham, pig, palette);
        SPDLOG_INFO("Writing {} textures to cache {}", cache.Entries.size(), D1_CACHE);
        cache.Write(D1_CACHE);
    }
}
