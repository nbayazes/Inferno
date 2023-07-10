#include "pch.h"
#include "Fonts.h"
#include <span>
#include "Streams.h"

namespace Inferno {
    constexpr ubyte BitsToBytes(ubyte x) { return (x + 7) >> 3; }

    /* File structure
    * 8 byte file header
    * 28 byte font header
    * characters * 2 byte width table @ widthsOffset
    * characters * 3 byte kerning table @ kerningOffset
    * variable len bitmap data @ dataOffset
    * 256 * 3 byte palette
    */
    Font Font::Read(span<ubyte> data) {
        StreamReader stream(data);
        if (stream.ReadString(4) != "PSFN") // Parallax Software FoNt
            throw Exception("Not a font file");

        auto datasize = stream.ReadInt32();
        auto headerOffset = stream.Position();

        Font font{};
        font.Width = stream.ReadInt16();
        font.Height = stream.ReadInt16();
        font.Flags = (FontFlags)stream.ReadInt16();
        font.Baseline = stream.ReadInt16();
        font.MinChar = stream.ReadByte();
        font.MaxChar = stream.ReadByte();
        /*auto byteWidth =*/ stream.ReadInt16();
        auto dataOffset = stream.ReadUInt32();
        /*auto reserved =*/ stream.ReadInt32();
        auto widthsOffset = stream.ReadInt32();
        auto kerningOffset = stream.ReadInt32();

        stream.Seek(headerOffset); // all offsets start after header
        auto buffer = stream.ReadUBytes(datasize); // read rest of the file
        font.Data = { buffer.begin() + dataOffset, buffer.end() }; // copy bitmap data

        if (font.Flags & FT_PROPORTIONAL) {
            int nchars = font.MaxChar - font.MinChar + 1;
            // The way widths are copied isn't obvious and probably unsafe. Uses pointer arithmetic.
            auto* widths = std::bit_cast<int16*>(&buffer[widthsOffset]); // interpret element stride as int16
            font.Widths = { widths, widths + nchars }; // init vector using start and end pointers

            int characterBitmapOffset = 0;
            for (int i = 0; i < nchars; i++) {
                int16 width = font.Widths[i];
                assert(width > 0 && width < 255);
                font.DataOffsets.push_back(characterBitmapOffset);

                // move offset to the next character based on the size of this one
                if (font.Flags & FT_COLOR)
                    characterBitmapOffset += font.Height * width;
                else
                    characterBitmapOffset += font.Height * BitsToBytes((ubyte)width);
            }
        }

        if (font.Flags & FT_KERNED) {
            auto iter = buffer.begin() + kerningOffset;
            while (*iter != 0xff) {
                ubyte c0 = iter[0] + font.MinChar;
                ubyte c1 = iter[1] + font.MinChar;
                font.Kernings.push_back({ c0, c1, iter[2] });
                iter += 3;
            }
        }

        if (font.Flags & FT_COLOR) {
            constexpr int PaletteSize = 256 * 3;
            stream.Seek(data.size() - PaletteSize); // palette is at end of file
            auto palette = stream.ReadUBytes(PaletteSize);
            font.Palette = ReadPalette(palette);
        }

        return font;
    }

    int FontAtlas::GetKerning(uchar c, uchar next, FontSize font) const {
        auto& f = _fonts[(int)font];
        if (f.Flags & Font::FT_KERNED && next != 0) {
            auto& kernings = f.Kernings;
            for (auto& k : kernings) {
                if (k.FirstChar == c && k.SecondChar == next)
                    return k.NewWidth - f.Widths[c - 32];
            }
        }

        return 0;
    }

    void FontAtlas::AddFont(span<Palette::Color> dest, Font& font, FontSize fontSize, int padding) {
        ubyte nchars = font.MaxChar - font.MinChar + 1;
        // todo: store font height so if next font is shorter is doesn't wrap around and overwrite it

        auto AddCharacter = [this, fontSize](uint x, uint y, uint width, uint height) {
            // UV 0,0 is top left
            float x0 = (float)x / _width;
            float x1 = ((float)x + width) / _width;
            float y0 = (float)y / _height;
            float y1 = ((float)y + height) / _height;
            _lookup[(int)fontSize].push_back({ x0, y0, x1, y1 });
        };

        for (ubyte c = 0; c < nchars; c++) {
            int width = font.GetWidth(c + font.MinChar);
            auto offset = font.DataOffsets[c];

            if (_x + width >= _width) {
                _x = 0;
                _y += font.Height + padding;
            }

            if (font.Flags & Font::FT_COLOR) {
                for (int y = 0; y < font.Height; y++) {
                    for (int x = 0; x < width; x++) {
                        auto index = font.Data[offset++];
                        if (index != 0xff) {
                            int bmpIndex = (_y + y) * _width + _x + x;
                            if (bmpIndex >= dest.size()) {
                                //SPDLOG_WARN("Texture atlas ran out of space!");
                                return;
                            }
                            dest[bmpIndex] = font.Palette.Data[index];
                        }
                    }
                }
            }
            else {
                for (int y = 0; y < font.Height; y++) {
                    int bits = 0, bitMask = 0;
                    for (int x = 0; x < width; x++) {
                        if (bitMask == 0) {
                            bits = font.Data[offset++];
                            bitMask = 1 << 7; // scan the next 8 bits
                        }

                        if (bits & bitMask) {
                            int bmpIndex = (_y + y) * _width + _x + x;
                            if (bmpIndex >= dest.size()) {
                                //SPDLOG_WARN("Texture atlas ran out of space!");
                                return;
                            }
                            dest[bmpIndex] = { 255, 255, 255, 255 };
                        }

                        bitMask >>= 1; // next bit
                    }
                }
            }

            AddCharacter(_x, _y, width, font.Height);
            _x += width + padding;
        }

        font.Data.clear(); // we don't need font data after adding it to the atlas texture
        font.DataOffsets.clear();
        _fonts[(int)fontSize] = font; // keep font metadata
    }
}
