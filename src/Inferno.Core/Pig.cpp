#include "pch.h"
#include "Pig.h"
#include "Streams.h"
#include "Utility.h"
#include "Sound.h"
#include <ranges>

namespace Inferno {
    constexpr auto PIGFILE_VERSION = 2;

    constexpr auto DBM_NUM_FRAMES = 63;
    constexpr auto DBM_FLAG_ABM = 64; // animated bitmap
    constexpr auto DBM_FLAG_LARGE = 128; // d1 bitmaps wider than 256

    constexpr uint8 RLE_CODE = 0xe0; // marks the end of RLE
    constexpr uint8 NOT_RLE_CODE = 0x1f;
    static_assert((RLE_CODE | NOT_RLE_CODE) == 0xff, "RLE mask error");

    constexpr uint8 SUPER_ALPHA = 128;

    constexpr int IsRleCode(uint8 x) {
        return (x & RLE_CODE) == RLE_CODE;
    }

    PigEntry ReadD1BitmapHeader(StreamReader& reader, TexID id) {
        PigEntry entry = {};
        entry.Name = reader.ReadString(8);
        auto dflags = reader.ReadByte();
        entry.Width = reader.ReadByte();
        entry.Height = reader.ReadByte();
        if (dflags & DBM_FLAG_LARGE)
            entry.Width += 256;

        auto flags = (BitmapFlag)reader.ReadByte();
        entry.AvgColor = reader.ReadByte();
        entry.DataOffset = reader.ReadInt32();
        entry.ID = id;
        entry.SetAnimationFlags(dflags);
        entry.SetFlags(flags);
        return entry;
    }

    PigEntry ReadD2BitmapHeader(StreamReader& reader, TexID id) {
        PigEntry entry = {};
        entry.Name = reader.ReadString(8);
        auto animFlags = reader.ReadByte();
        entry.Width = reader.ReadByte();
        entry.Height = reader.ReadByte();
        auto rleExtra = reader.ReadByte();
        auto flags = reader.ReadByte();
        entry.AvgColor = reader.ReadByte();
        entry.DataOffset = reader.ReadInt32();

        entry.SetAnimationFlags(animFlags);
        entry.SetFlags((BitmapFlag)flags);
        entry.ID = id;

        entry.Width += (ushort)(rleExtra % 16) * 256;
        if (flags & 0x80 && entry.Width > 256)
            entry.Height *= entry.Width;
        else
            entry.Height += (ushort)(rleExtra / 16) * 256;

        return entry;
    }

    Dictionary<TexID, PigBitmap> ReadPoggies(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette) {
        Dictionary<TexID, PigBitmap> bitmaps;

        StreamReader reader(data);

        auto fileId = reader.ReadInt32();
        auto version = reader.ReadInt32();
        if (fileId != 'GOPD' || version != 1) {
            //SPDLOG_WARN("POG entry has incorrect header");
            return bitmaps;
        }

        List<TexID> ids;
        ids.resize(reader.ReadElementCount());

        for (auto& id : ids) {
            id = (TexID)reader.ReadInt16();
            if ((int)id > pigEntries.size()) {
                //SPDLOG_WARN("POG out of range index {}", id);
                return bitmaps;
            }
        }

        for (auto& id : ids)
            pigEntries[(int)id] = ReadD2BitmapHeader(reader, id);

        auto dataStart = reader.Position();

        for (auto& id : ids) {
            auto& entry = pigEntries[(int)id];
            bitmaps[id] = ReadBitmapEntry(reader, dataStart, entry, palette);
        }

        return bitmaps;
    }

    // DTX patches are similar to POGs, but for D1
    Dictionary<TexID, PigBitmap> ReadDTX(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette) {
        StreamReader reader(data);

        auto nBitmaps = reader.ReadInt32();
        auto nSounds = reader.ReadInt32();

        List<PigEntry> entries;
        entries.resize(nBitmaps);

        for (auto& entry : entries) {
            entry = ReadD1BitmapHeader(reader, (TexID)0);
            auto existing = Seq::find(pigEntries, [&entry](PigEntry& e) { return e.Name == entry.Name; });
            if (existing) {
                entry.ID = existing->ID;
                *existing = entry;
            }
        }

        List<SoundFile::Header> sounds;
        sounds.resize(nSounds);

        for (auto& sound : sounds)
            sound = ReadSoundHeader(reader);

        auto dataStart = reader.Position();

        Dictionary<TexID, PigBitmap> bitmaps;

        for (auto& entry : entries)
            bitmaps[entry.ID] = ReadBitmapEntry(reader, dataStart, entry, palette);

        // There's sound data here but we don't care

        return bitmaps;
    }

    PigFile ReadPigFile(wstring file) {
        StreamReader reader(file);
        PigFile pig;
        pig.Path = file;

        //make sure pig is valid type file & is up-to-date
        auto sig = (uint)reader.ReadInt32();
        auto version = reader.ReadInt32();
        if (sig != MakeFourCC("PPIG") || version != PIGFILE_VERSION)
            throw Exception("PIG file is not valid");

        auto nBitmaps = reader.ReadInt32();
        constexpr int BitmapHeaderSize = 18;
        auto headerSize = nBitmaps * BitmapHeaderSize;
        pig.DataStart = headerSize + reader.Position();
        pig.Entries.resize(nBitmaps + 1);

        int i = 1; // 0 is reserved for errors
        for (auto& entry : pig.Entries | std::views::drop(1))
            entry = ReadD2BitmapHeader(reader, (TexID)i++);

        return pig;
    }

    // Permanently flips image data along Y axis
    void FlipBitmapY(PigBitmap& bmp) {
        List<ubyte> buffer(bmp.Width * bmp.Height * 4);
        int rowLen = (int)sizeof(ubyte) * 4 * bmp.Width;
        int i = 0;
        for (int row = bmp.Height - 1; row >= 0; row--) {
            memcpy(&buffer[i], &bmp.Data[(size_t)row * bmp.Width], rowLen);
            i += rowLen;
        }

        memcpy(bmp.Data.data(), buffer.data(), buffer.size());
    }

    void ExtractMask(PigBitmap& bmp) {
        auto size = bmp.Data.size();
        bmp.Mask.resize(size);

        for (size_t i = 0; i < size; i++) {
            if (bmp.Data[i].a == SUPER_ALPHA) {
                bmp.Mask[i] = Palette::SUPER_MASK;
                bmp.Data[i] = { 0, 0, 0, 0 }; // clear the source pixel
            }
            else {
                bmp.Mask[i] = Palette::TRANSPARENT_MASK;
            }
        }
    }

    void CheckTransparency(Palette::Color& color, ubyte palIndex) {
        if (palIndex >= 254) {
            color = { 0, 0, 0, 0 }; // Using premultiplied alpha...
            if (palIndex == 254) {
                color.a = SUPER_ALPHA;
            }
        }
    }

    PigBitmap ReadRLE(StreamReader& reader,
                      size_t dataStart,
                      const Palette& palette,
                      const PigEntry& entry) {
        reader.Seek(dataStart + entry.DataOffset);
        reader.ReadInt32(); // size

        PigBitmap bmp(entry.Width, entry.Height, entry.Name);
        List<uint16> rowSize(bmp.Height);
        List<uint8> buffer(bmp.Width * 3);
        bmp.Data.resize((size_t)bmp.Width * bmp.Height);

        if (entry.UsesBigRle) {
            // long scan lines (>= 256 bytes), row lengths are stored as shorts
            reader.ReadBytes(rowSize.data(), bmp.Height * sizeof(int16));
        }
        else {
            // row lengths are stored as bytes
            for (int i = 0; i < bmp.Height; i++)
                rowSize[i] = reader.ReadByte() & 0xff;
        }

        for (int y = bmp.Height - 1, row = 0; y >= 0; y--, row++) {
            reader.ReadBytes(buffer.data(), rowSize[row]);
            int h = y * bmp.Width;
            for (int x = 0, offset = 0; x < bmp.Width;) {
                auto palIndex = buffer[offset++]; // palette index
                if (IsRleCode(palIndex)) {
                    auto runLength = std::min(palIndex & ~RLE_CODE, bmp.Width - x);
                    palIndex = buffer[offset++];
                    Palette::Color color = palette.Data[palIndex];
                    CheckTransparency(color, palIndex);

                    for (int j = 0; j < runLength; j++, x++, h++)
                        bmp.Data[h] = color;
                }
                else {
                    bmp.Data[h] = palette.Data[palIndex];
                    CheckTransparency(bmp.Data[h], palIndex);
                    x++, h++;
                }
            }
        }

        return bmp;
    }

    PigBitmap ReadBMP(StreamReader& reader,
                      size_t dataStart,
                      const Palette& palette,
                      const PigEntry& entry) {
        reader.Seek(dataStart + entry.DataOffset);

        PigBitmap bmp(entry.Width, entry.Height, entry.Name);
        bmp.Data.resize((size_t)bmp.Width * bmp.Height);
        for (int y = bmp.Height - 1; y >= 0; y--) {
            int h = y * bmp.Width;
            for (int x = 0; x < bmp.Width; x++, h++) {
                auto palIndex = reader.ReadByte();
                bmp.Data[h] = palette.Data[palIndex];
                CheckTransparency(bmp.Data[h], palIndex);
            }
        }

        return bmp;
    }

    PigBitmap ReadBitmapEntry(StreamReader& reader,
                              size_t dataStart,
                              const PigEntry& entry,
                              const Palette& palette) {
        auto bmp = entry.UsesRle ?
            ReadRLE(reader, dataStart, palette, entry) :
            ReadBMP(reader, dataStart, palette, entry);

        FlipBitmapY(bmp);
        if (entry.SuperTransparent)
            ExtractMask(bmp);

        return bmp;
    }

    PigBitmap ReadBitmap(const PigFile& pig, const Palette& palette, TexID id) {
        auto index = (int)id;
        if (pig.Entries.empty()) return { 0, 0, "" };
        if (index >= pig.Entries.size() || index < 0) index = 0;

        auto& entry = pig.Entries[index];

        StreamReader reader(pig.Path);
        return ReadBitmapEntry(reader, pig.DataStart, entry, palette);
    }

    List<PigBitmap> ReadAllBitmaps(const PigFile& pig, const Palette& palette) {
        List<PigBitmap> bitmaps;

        StreamReader reader(pig.Path);
        for (auto& entry : pig.Entries)
            bitmaps.push_back(ReadBitmapEntry(reader, pig.DataStart, entry, palette));

        return bitmaps;
    }

    Palette ReadPalette(span<ubyte> data) {
        // It does not read the fade table from the file.
        Palette palette;
        if (data.size() < 256 * 3) throw Exception("Palette is missing data");

        // decode
        for (int i = 0, j = 0; i < 256; i++) {
            palette.Data[i].r = data[j++] * 4;
            palette.Data[i].g = data[j++] * 4;
            palette.Data[i].b = data[j++] * 4;
        }

        palette.SuperTransparent = palette.Data[254];
        auto FadeValue = [](ubyte c, int f) { return (ubyte)(((int)c * f) / 34); };

        //create fade table
        for (int i = 0; i < 256; i++) {
            ubyte c = data[i];
            for (int j = 0; j < 34; j++)
                palette.FadeTables[j * palette.Data.size() + i] = FadeValue(c, j + 1);
        }

        return palette;
    }
}
