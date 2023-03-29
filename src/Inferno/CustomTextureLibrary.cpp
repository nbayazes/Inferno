#include "pch.h"
#include "CustomTextureLibrary.h"
#include "Resources.h"

namespace Inferno {
    constexpr std::array ROBOT_TEXTURES = {
        "rbot", "eye", "glow", "boss", "metl", "ctrl", "react", "rmap", "ship",
        "energy01", "flare", "marker", "missile", "missiles", "missback", "water07"
    };

    constexpr std::array POWERUP_TEXTURES = {
        "aftrbrnr", "allmap", "ammorack", "cloak", "cmissil*", "convert", "erthshkr",
        "flag01", "flag02", "fusion", "gauss", "headlite", "helix", "hmissil", "hostage",
        "invuln", "key01", "key02", "key03", "laser", "life01", "merc", "mmissile",
        "omega", "pbombs", "phoenix", "plasma", "quad", "spbombs", "spread", "suprlasr",
        "vammo", "vulcan"
    };

    TextureType ClassifyTexture(const PigEntry& entry) {
        if (Resources::GameData.LevelTexIdx[(int)entry.ID] != LevelTexID(255))
            return TextureType::Level;

        for (auto& filter : ROBOT_TEXTURES) {
            if (String::InvariantEquals(entry.Name, filter, strlen(filter)))
                return TextureType::Robot;
        }

        for (auto& filter : POWERUP_TEXTURES) {
            if (String::InvariantEquals(entry.Name, filter, strlen(filter)))
                return TextureType::Powerup;
        }

        return TextureType::Misc;
    }

    string RemoveFrameNumber(string name) {
        if (auto index = String::IndexOf(name, "#"))
            name = name.substr(0, *index);

        return name;
    }

    void WriteD1BitmapHeader(StreamWriter& writer, const PigEntry& entry) {
        auto width = entry.Width;
        if (width > 256) width -= 256;

        writer.WriteString(RemoveFrameNumber(entry.Name), 8);
        writer.Write<ubyte>(entry.GetD1Flags());
        writer.Write((uint8)width);
        writer.Write((uint8)entry.Height);
        writer.Write((uint8)entry.GetFlags());
        writer.Write<ubyte>(entry.AvgColor);
        writer.Write<uint32>(entry.DataOffset);
    }

    void WriteD2BitmapHeader(StreamWriter& writer, const PigEntry& entry) {
        writer.WriteString(RemoveFrameNumber(entry.Name), 8);
        writer.Write<ubyte>(entry.GetD2Flags());
        writer.Write((uint8)entry.Width);
        writer.Write((uint8)entry.Height);
        uint8 rleExtra = (entry.Width >> 8) | ((entry.Height >> 4) & 0xF0);
        writer.Write<uint8>(rleExtra);
        writer.Write((uint8)entry.GetFlags());
        writer.Write<ubyte>(entry.AvgColor);
        writer.Write<uint32>(entry.DataOffset);
    }

    void CustomTextureLibrary::ImportBmp(const filesystem::path& path, bool transparent, PigEntry entry, bool descent1, bool whiteAsTransparent) {
        StreamReader stream(path);
        BITMAPFILEHEADER bmfh{};
        BITMAPINFOHEADER bmih{};

        bool topDown = false;
        stream.ReadBytes(&bmfh, sizeof bmfh);
        stream.ReadBytes(&bmih, sizeof bmih);

        if (bmih.biClrUsed == 0)
            bmih.biClrUsed = 256;

        if (bmih.biHeight < 0) {
            bmih.biHeight = -bmih.biHeight;
            topDown = true;
        }

        auto paletteSize = (int)bmih.biClrUsed;
        if (paletteSize == 0)
            paletteSize = 1 << bmih.biBitCount;

        if (bmfh.bfType != 'MB')
            throw Exception("Not a bitmap file");

        if ((bmih.biBitCount != 8 && bmih.biBitCount != 4) || paletteSize != 256)
            throw Exception("Only 256 indexed color bitmap files are supported");

        if (bmih.biCompression != BI_RGB)
            throw Exception("Cannot read compressed bitmaps. Resave the file with compression turned off.");

        if (ClassifyTexture(entry) == TextureType::Level) {
            if (bmih.biWidth != bmih.biHeight || !IsPowerOfTwo(bmih.biWidth))
                throw Exception("Level textures must be square and a power of 2, otherwise texmerge will fail.");
        }

        if (descent1) {
            if (bmih.biWidth > 511 || bmih.biHeight > 255)
                throw Exception("Descent 1 DTX files support a max resolution of 511 x 255");
        }
        else {
            if (bmih.biWidth > 4095 || bmih.biHeight > 4095)
                throw Exception("Descent 2 POG files support a max resolution of 4095 x 4095");
        }

        stream.Seek(sizeof(BITMAPFILEHEADER) + bmih.biSize);

        // read palette
        Palette bmpPalette;
        for (auto& color : bmpPalette.Data) {
            color.b = stream.ReadByte(); // RGBQUAD ordering
            color.g = stream.ReadByte();
            color.r = stream.ReadByte();
            stream.ReadByte(); // reserved;
        }

        PigBitmap bmp(entry);
        bmp.Info.Width = (uint16)bmih.biWidth;
        bmp.Info.Height = (uint16)bmih.biHeight;
        bmp.Data.resize(bmih.biWidth * bmih.biHeight);
        bmp.Indexed.resize(bmih.biWidth * bmih.biHeight);

        auto& gamePalette = Resources::GetPalette();
        PaletteLookup lookup(gamePalette);

        // Index closest to white when using the "white as transparent" option
        PaletteLookup bmpLookup(bmpPalette);
        auto whiteIndex = bmpLookup.GetClosestIndex({ 255, 255, 255 }, true);

        // read data into bitmap
        int width = ((int)(bmih.biWidth * bmih.biBitCount + 31) >> 3) & ~3;
        int z = 0;
        for (int y = 0; y < bmp.Info.Height; y++) {
            for (int x = 0; x < bmp.Info.Width; x++, z++) {
                int u = x;
                int v = !topDown ? bmih.biHeight - y - 1 : y;
                ubyte palIndex{};

                if (bmih.biBitCount == 4) {
                    long offset = v * width + u / 2;
                    stream.Seek((int)bmfh.bfOffBits + offset);
                    palIndex = stream.ReadByte();
                    if (!(u & 1))
                        palIndex >>= 4;
                    palIndex &= 0x0f;
                }
                else {
                    stream.Seek((int)bmfh.bfOffBits + v * width + u);
                    palIndex = stream.ReadByte();
                }

                auto& c = bmpPalette.Data[palIndex];
                bmp.Indexed[z] = lookup.GetClosestIndex(c, transparent);
                bmp.Data[z] = gamePalette.Data[bmp.Indexed[z]];

                if (transparent) {
                    Palette::CheckTransparency(bmp.Data[z], palIndex);

                    if (palIndex >= Palette::ST_INDEX)
                        bmp.Info.Transparent = true;

                    if (palIndex == Palette::ST_INDEX)
                        bmp.Info.SuperTransparent = true;
                }

                if (whiteAsTransparent && palIndex == whiteIndex) {
                    bmp.Indexed[z] = Palette::T_INDEX;
                    bmp.Data[z] = { 0, 0, 0, 0 };
                    bmp.Info.Transparent = true;
                }
            }
        }

        bmp.Info.AverageColor = GetAverageColor(bmp.Data);
        bmp.Info.Custom = true;
        bmp.ExtractMask();

        SPDLOG_INFO(L"Loaded BMP {}x{} from {}", bmp.Info.Width, bmp.Info.Height, path.wstring());
        _textures[entry.ID] = std::move(bmp);
    }

    void WriteBitmap(StreamWriter& writer, PaletteLookup& lookup, const PigBitmap& bitmap) {
        for (auto& pixel : bitmap.Data) {
            // convert color to index and write byte
            auto index = lookup.GetClosestIndex(pixel, bitmap.Info.Transparent);
            writer.Write<ubyte>(index);
        }
    }

    size_t CustomTextureLibrary::WritePog(StreamWriter& writer, const Palette& palette) {
        if (_textures.empty()) return 0;
        auto startPos = writer.Position();
        writer.Write<int32>('GOPD'); // Descent POG
        writer.Write<int32>(1);
        writer.Write((int32)_textures.size());

        auto ids = GetSortedIds();

        for (auto& id : ids)
            writer.Write((int16)id);

        uint32 offset = 0;
        for (auto& id : ids) {
            auto& entry = _textures[id].Info;
            entry.UsesRle = false; // the serializer does not support RLE
            entry.DataOffset = offset;
            WriteD2BitmapHeader(writer, entry);
            offset += entry.Width * entry.Height; // bytes
        }

        // write bitmap data
        PaletteLookup lookup(palette);
        for (auto& id : ids)
            writer.WriteBytes(_textures[id].Indexed);

        return writer.Position() - startPos;
    }

    size_t CustomTextureLibrary::WriteDtx(StreamWriter& writer, const Palette& palette) {
        auto startPos = writer.Position();
        writer.Write((int32)_textures.size());
        writer.Write<int32>(0); // Sound count

        auto ids = GetSortedIds();
        uint32 offset = 0;

        for (auto& id : ids) {
            auto& entry = _textures[id].Info;
            entry.UsesRle = false; // the serializer does not support RLE
            entry.DataOffset = offset;
            WriteD1BitmapHeader(writer, entry);
            offset += entry.Width * entry.Height; // bytes
        }

        // Sound headers would be here but are omitted

        // write bitmap data
        PaletteLookup lookup(palette);
        for (auto& id : ids)
            writer.WriteBytes(_textures[id].Indexed);

        // Sound data would be here but is omitted
        return writer.Position() - startPos;
    }

    void CustomTextureLibrary::LoadPog(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette) {
        StreamReader reader(data);

        auto fileId = reader.ReadInt32();
        auto version = reader.ReadInt32();
        if (fileId != 'GOPD' || version != 1) {
            SPDLOG_WARN("POG file has incorrect header");
            return;
        }

        List<TexID> ids;
        ids.resize(reader.ReadElementCount());

        for (auto& id : ids) {
            id = (TexID)reader.ReadInt16();
            if ((int)id > pigEntries.size()) {
                SPDLOG_WARN("POG with out of range TexID: {}", id);
                return;
            }
        }

        for (auto& id : ids)
            pigEntries[(int)id] = ReadD2BitmapHeader(reader, id);

        auto dataStart = reader.Position();

        for (auto& id : ids) {
            auto& entry = pigEntries[(int)id];
            _textures[id] = ReadBitmapEntry(reader, dataStart, entry, palette);
            _textures[id].Info.Custom = true;
        }

        SPDLOG_INFO("Loaded {} custom textures from POG", ids.size());
    }

    void CustomTextureLibrary::LoadDtx(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette) {
        StreamReader reader(data);

        auto nBitmaps = reader.ReadInt32();
        auto nSounds = reader.ReadInt32();

        List<PigEntry> entries;
        entries.resize(nBitmaps);

        for (auto& entry : entries) {
            entry = ReadD1BitmapHeader(reader, (TexID)0);
            // Unfortunately textures are replaced by name instead of index
            auto existing = Seq::find(pigEntries, [&entry](const PigEntry& e) { return e.Name == entry.Name; });
            if (existing) {
                entry.ID = existing->ID;
                entry.Custom = true;
                *existing = entry;
            }
        }

        List<SoundFile::Header> sounds;
        sounds.resize(nSounds);

        for (auto& sound : sounds)
            sound = ReadSoundHeader(reader);

        auto dataStart = reader.Position();

        for (auto& entry : entries) {
            _textures[entry.ID] = ReadBitmapEntry(reader, dataStart, entry, palette);
        }

        // There's sound data here but we don't care

        SPDLOG_INFO("Loaded {} custom textures from DTX", nBitmaps);
    }
}
