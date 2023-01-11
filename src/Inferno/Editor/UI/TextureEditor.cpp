#include "pch.h"
#include "TextureEditor.h"

namespace Inferno::Editor {
    struct BitmapMetrics {
        int Width = 0, Height = 0;
        int Colors = 0;
        bool MatchesPalette = false;
    };

    class PaletteLookup {
        const Palette& _palette;
        Dictionary<uint32, ubyte> _cache; // maps color to index
    public:
        PaletteLookup(const Palette& palette) : _palette(palette) {}

        ubyte GetClosestIndex(const Palette::Color& color, bool transparent) {
            uint hash = (int)color.r + ((int)color.g << 8) + ((int)color.b << 16);
            if (_cache.contains(hash))
                return _cache[hash];

            uint closestDelta = 0x7fffffff;
            ubyte closestIndex = 0;

            for (int i = 0; i < (transparent ? 256 : 254); i++) {
                uint delta = color.Delta(_palette.Data[i]);
                if (delta < closestDelta) {
                    closestIndex = (ubyte)i;
                    if (delta == 0)
                        break;
                    closestDelta = delta;
                }
            }

            _cache[hash] = closestIndex;
            return closestIndex;
        }
    };

    void WriteBmp(const filesystem::path& path, const Palette& gamePalette, const PigBitmap& bmp) {
        std::ofstream stream(path, std::ios::binary);
        StreamWriter writer(stream, false);

        constexpr DWORD offset = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 256 * 4;

        BITMAPFILEHEADER bmfh{
            .bfType = 'MB',
            .bfSize = offset + bmp.Info.Width * bmp.Info.Height,
            .bfOffBits = offset
        };

        BITMAPINFOHEADER bmih{
            .biSize = sizeof(BITMAPINFOHEADER), // (DWORD)(bmp.Width * bmp.Height)
            .biWidth = bmp.Info.Width,
            .biHeight = bmp.Info.Height,
            .biPlanes = 1,
            .biBitCount = 8,
            .biCompression = BI_RGB,
            .biSizeImage = 0, // 64*64*4
            .biXPelsPerMeter = 0,
            .biYPelsPerMeter = 0,
            .biClrUsed = 256,
            .biClrImportant = 0
        };

        writer.WriteBytes(span{ (ubyte*)&bmfh, sizeof bmfh });
        writer.WriteBytes(span{ (ubyte*)&bmih, sizeof bmih });

        List<RGBQUAD> palette(256);

        for (auto& color : gamePalette.Data) {
            writer.Write<RGBQUAD>({ color.b, color.g, color.r });
        }

        writer.WriteBytes(span{ (ubyte*)bmp.Indexed.data(), bmp.Indexed.size() });
    }


    void LoadBmp(const filesystem::path& path, bool transparent, PigEntry entry) {
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

        if (bmfh.bfType != 'MB')
            throw Exception("Not a bitmap file");

        if (bmih.biBitCount != 8 && bmih.biBitCount != 4)
            throw Exception("Only 16 or 256 color bitmap files are supported");

        if (bmih.biCompression != BI_RGB)
            throw Exception("Cannot read compressed bitmaps. Resave the file with compression turned off.");

        // read palette
        auto paletteSize = (int)bmih.biClrUsed;
        if (paletteSize == 0)
            paletteSize = 1 << bmih.biBitCount;

        List<RGBQUAD> palette(paletteSize);
        stream.ReadBytes(palette.data(), palette.size() * sizeof(RGBQUAD));

        PigBitmap bmp(entry);
        bmp.Info.Width = (uint16)bmih.biWidth;
        bmp.Info.Height = (uint16)bmih.biHeight;
        bmp.Data.resize(bmih.biWidth * bmih.biHeight);
        bmp.Indexed.resize(bmih.biWidth * bmih.biHeight);

        auto& gamePalette = Resources::GetPalette();
        PaletteLookup lookup(gamePalette);

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

                auto& c = palette[palIndex];
                bmp.Indexed[z] = lookup.GetClosestIndex({ c.rgbRed, c.rgbGreen, c.rgbBlue }, transparent);
                bmp.Data[z] = gamePalette.Data[bmp.Indexed[z]];

                if (transparent) {
                    Palette::CheckTransparency(bmp.Data[z], palIndex);

                    if (palIndex >= Palette::ST_INDEX)
                        bmp.Info.Transparent = true;

                    if (palIndex == Palette::ST_INDEX)
                        bmp.Info.SuperTransparent = true;
                }
            }
        }

        bmp.Info.AverageColor = GetAverageColor(bmp.Data);
        bmp.Info.Custom = true;
        bmp.ExtractMask();

        //auto bmp = LoadBMP(reader, Resources::GetPalette(), transparent);
        SPDLOG_INFO(L"Loaded BMP {}x{} from {}", bmp.Info.Width, bmp.Info.Height, path.c_str());
        Resources::CustomTextures[entry.ID] = std::move(bmp);
    }

    enum class TextureType {
        Level, Robot, Powerup, Misc
    };

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

    void TextureEditor::UpdateTextureList() {
        _visibleTextures.clear();

        auto levelTextures = Render::GetLevelSegmentTextures(Game::Level);

        for (int i = 1; i < Resources::GetTextureCount(); i++) {
            auto& bmp = Resources::GetBitmap((TexID)i);
            auto type = ClassifyTexture(bmp.Info);

            if ((_showModified && bmp.Info.Custom) ||
                (_showInUse && levelTextures.contains(bmp.Info.ID))) {
                // show if modified or in use
            }
            else {
                if (!_showRobots && type == TextureType::Robot)
                    continue;

                if (!_showPowerups && type == TextureType::Powerup)
                    continue;

                if (!_showMisc && type == TextureType::Misc)
                    continue;

                if (!_showLevel && type == TextureType::Level)
                    continue;
            }

            _visibleTextures.push_back((TexID)i);
        }

        if (!Seq::contains(_visibleTextures, _selection))
            _selection = _visibleTextures.empty() ? TexID::None : _visibleTextures.front();
    }

    void WriteD2BitmapHeader(StreamWriter& writer, const PigEntry& entry) {
        writer.WriteString(entry.Name.c_str(), 8);
        writer.Write<ubyte>(entry.GetD2Flags());
        writer.Write((uint8)entry.Width);
        writer.Write((uint8)entry.Height);
        uint8 rleExtra = (entry.Width >> 8) | ((entry.Height >> 4) & 0xF0);
        writer.Write<uint8>(rleExtra);
        writer.Write((uint8)entry.GetFlags());
        writer.Write<ubyte>(entry.AvgColor);
        writer.Write<uint32>(entry.DataOffset);
    }

    void WritePog(span<PigBitmap> textures, StreamWriter& writer, const Palette& palette) {
        if (textures.empty()) return;
        writer.Write<int32>('GOPD'); // Descent POG
        writer.Write<int32>(1);
        writer.Write<int32>(textures.size());
        for (auto& entry : textures) {
            writer.Write((int16)entry.Info.ID);
        }

        uint32 offset = 0;
        for (auto& entry : textures) {
            entry.Info.DataOffset = offset;
            WriteD2BitmapHeader(writer, entry.Info);
            offset += entry.Info.Width * entry.Info.Height; // bytes
        }

        PaletteLookup lookup(palette);

        // write bitmap data

        //for (int i = 0; i < textures.size(); i++) {
        //    auto& bmp = bmps[i];
        //    auto& entry = textures[i];

        //    for (auto& pixel : bmp.Data) {
        //        // convert color to index and write byte
        //        auto index = lookup.GetClosestColor(pixel, entry.Transparent);
        //        writer.Write<ubyte>(index);
        //    }
        //}

        for (auto& texture : textures) {
            for (auto& pixel : texture.Data) {
                // convert color to index and write byte
                auto index = lookup.GetClosestIndex(pixel, texture.Info.Transparent);
                writer.Write<ubyte>(index);
            }
        }
    }
}
