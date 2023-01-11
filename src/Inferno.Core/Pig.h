#pragma once

#include "Types.h"
#include "Streams.h"

namespace Inferno {
    struct Palette {
        struct Color {
            ubyte r = 0, g = 0, b = 0, a = 255;
            constexpr uint Delta(const Color& rhs) const {
                constexpr auto sqr = [](auto x) { return x * x; };
                return sqr((int)r - (int)rhs.r) + sqr((int)g - (int)rhs.g) + sqr((int)b - (int)rhs.b);
            }
        };

        Color SuperTransparent;
        List<ubyte> FadeTables;
        List<Color> Data;

        static constexpr int ST_INDEX = 254; // Supertransparent palette index
        static constexpr int T_INDEX = 255; // Transparent palette index
        static constexpr uint8 SUPER_ALPHA = 128; // Value used for the supertransparent mask

        static void CheckTransparency(Palette::Color& color, ubyte palIndex);

        Palette() : FadeTables(34 * 256), Data(256) {}
    };

    constexpr Color GetAverageColor(span<const Palette::Color> data) {
        int red = 0, green = 0, blue = 0, count = 0;

        for (auto& d : data) {
            if (d.a < Palette::ST_INDEX) continue;
            red += d.r;
            green += d.g;
            blue += d.b;
            count++;
        }

        if (count > 0) {
            red /= count;
            green /= count;
            blue /= count;

            return { (float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f, 1 };
        }

        return { 0, 0, 0, 1 };
    }

    enum class BitmapFlag : uint8 {
        None = 0,
        Transparent = 1,
        SuperTransparent = 2,
        NoLighting = 4,
        Rle = 8,        // A run-length encoded bitmap.
        PagedOut = 16,  // This bitmap's data is paged out.
        RleBig = 32     // for bitmaps that RLE to > 255 per row (i.e. cockpits)
    };

    struct PigEntry {
        string Name;
        uint16 Width, Height;
        ubyte AvgColor;
        Color AverageColor;
        uint32 DataOffset;

        bool Transparent;
        // When used as an overlay texture, super transparency forces areas of the base texture to be transparent.
        bool SuperTransparent;
        bool UsesRle;
        bool UsesBigRle;
        bool Animated;
        uint8 Frame; // The frame index in an animation
        TexID ID = TexID::None;
        bool Custom = false; // Texture was loaded from a DTX or POG

        void SetFlags(BitmapFlag flags) {
            Transparent = bool(flags & BitmapFlag::Transparent);
            SuperTransparent = bool(flags & BitmapFlag::SuperTransparent);
            UsesRle = bool(flags & BitmapFlag::Rle);
            UsesBigRle = bool(flags & BitmapFlag::RleBig);
        }

        void SetAnimationFlags(uint8 flags) {
            Animated = flags & AnimatedFlag;
            Frame = flags & FrameMask;

            if (Animated)
                Name = fmt::format("{}#{}", Name, Frame);
        }

        BitmapFlag GetFlags() const {
            //BitmapFlag flags{ Width > 256 ? 0x80 : 0 };
            BitmapFlag flags{};
            if (Transparent) flags |= BitmapFlag::Transparent;
            if (SuperTransparent) flags |= BitmapFlag::SuperTransparent;
            if (UsesRle) flags |= BitmapFlag::Rle;
            if (UsesBigRle) flags |= BitmapFlag::RleBig;
            return flags;
        }

        uint8 GetD2Flags() const {
            uint8 dflags{};
            if (Animated) dflags |= AnimatedFlag;
            if (Frame) dflags |= Frame & FrameMask;
            return dflags;
        }

    private:
        static constexpr uint8 FrameMask = 63;
        static constexpr uint8 AnimatedFlag = 64;
    };

    struct PigBitmap {
        List<Palette::Color> Mask; // Supertransparent mask
        List<Palette::Color> Data; // Resolved color data
        List<ubyte> Indexed; // Raw index data

        PigEntry Info{};
        void ExtractMask();

        PigBitmap() = default;
        PigBitmap(PigEntry entry) : Info(std::move(entry)) {}
        /*PigBitmap(uint16 width, uint16 height, string name) {
            Info.Width = width;
            Info.Height = height;
            Info.Name = std::move(name);
        }*/

        ~PigBitmap() = default;
        PigBitmap(const PigBitmap&) = delete;
        PigBitmap(PigBitmap&&) = default;
        PigBitmap& operator=(const PigBitmap&) = delete;
        PigBitmap& operator=(PigBitmap&&) = default;
    };


    // A texture file
    struct PigFile {
        wstring Path;
        size_t DataStart;
        List<PigEntry> Entries;

        const PigEntry& Get(TexID id) const {
            if ((int)id >= Entries.size() || (int)id < 0) return Entries[0];
            return Entries[(int)id];
        }

        PigFile() = default;
        ~PigFile() = default;
        PigFile(const PigFile&) = delete;
        PigFile(PigFile&&) = default;
        PigFile& operator=(const PigFile&) = delete;
        PigFile& operator=(PigFile&&) = default;
    };


    PigBitmap ReadBitmap(const PigFile& pig, const Palette& palette, TexID id);
    PigBitmap ReadBitmapEntry(StreamReader&, size_t dataStart, const PigEntry&, const Palette&);
    List<PigBitmap> ReadAllBitmaps(const PigFile& pig, const Palette& palette);

    Dictionary<TexID, PigBitmap> ReadDTX(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette);
    Dictionary<TexID, PigBitmap> ReadPoggies(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette);

    Palette ReadPalette(span<ubyte> data);
    PigFile ReadPigFile(wstring file);
    PigEntry ReadD2BitmapHeader(StreamReader&, TexID);
    PigEntry ReadD1BitmapHeader(StreamReader&, TexID);

}