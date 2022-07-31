#pragma once

#include "Types.h"
#include "Streams.h"

namespace Inferno {
    struct Palette {
        struct Color {
            ubyte r = 0, g = 0, b = 0, a = 255;
        };

        Color SuperTransparent;
        List<ubyte> FadeTables;
        List<Color> Data;

        constexpr Palette(int colors = 256) :
            FadeTables(34 * colors),
            Data(colors) {}
    };

    constexpr Color GetAverageColor(span<const Palette::Color> data) {
        int red = 0, green = 0, blue = 0, count = 0;

        for (auto& d : data) {
            if (d.a < 254) continue;
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

    struct PigBitmap {
        List<Palette::Color> Mask;
        List<Palette::Color> Data;

        uint16 Width, Height;
        string Name;

        PigBitmap() : Width(0), Height(0) {}
        PigBitmap(uint16 width, uint16 height, string name) 
            : Width(width), Height(height), Name(name) {}
        ~PigBitmap() = default;
        PigBitmap(const PigBitmap&) = delete;
        PigBitmap(PigBitmap&&) = default;
        PigBitmap& operator=(const PigBitmap&) = delete;
        PigBitmap& operator=(PigBitmap&&) = default;
    };

    struct PigEntry {
        string Name;
        uint16 Width, Height;
        ubyte AvgColor;
        Color AverageColor;
        int DataOffset;

        bool Transparent;
        // When used as an overlay texture, super transparency forces areas of the base texture to be transparent.
        bool SuperTransparent;
        bool UsesRle;
        bool UsesBigRle;
        bool Animated;
        uint8 Frame; // The frame index in an animation
        TexID ID = TexID::None;

        void SetFlags(BitmapFlag flags) {
            Transparent = (uint8)flags & (uint8)BitmapFlag::Transparent;
            SuperTransparent = (uint8)flags & (uint8)BitmapFlag::SuperTransparent;
            UsesRle = (uint8)flags & (uint8)BitmapFlag::Rle;
            UsesBigRle = (uint8)flags & (uint8)BitmapFlag::RleBig;
        }

        void SetAnimationFlags(uint8 flags) {
            Animated = flags & AnimatedFlag;
            Frame = flags & FrameMask;

            if (Animated)
                Name = fmt::format("{}#{}", Name, Frame);
        }
    private:
        static constexpr uint8 FrameMask = 63;
        static constexpr uint8 AnimatedFlag = 64;
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


    PigBitmap ReadBitmap(PigFile& pig, const Palette& palette, TexID id);
    PigBitmap ReadBitmapEntry(StreamReader&, size_t dataStart, const PigEntry&, const Palette&);
    List<PigBitmap> ReadAllBitmaps(PigFile& pig, const Palette& palette);

    Dictionary<TexID, PigBitmap> ReadDTX(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette);
    Dictionary<TexID, PigBitmap> ReadPoggies(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette);

    Palette ReadPalette(span<ubyte> data);
    PigFile ReadPigFile(wstring file);
    PigEntry ReadD2BitmapHeader(StreamReader&, TexID);
    PigEntry ReadD1BitmapHeader(StreamReader&, TexID);

}