#pragma once

#include "Pig.h"

namespace Inferno {
    struct Kerning {
        ubyte FirstChar; // If secondChar follows firstChar
        ubyte SecondChar;
        ubyte NewWidth; // The first character's width will be temporarily set to this value
    };

    struct Font {
        enum FontFlags : int16 {
            FT_COLOR = 1,
            FT_PROPORTIONAL = 2,
            FT_KERNED = 4
        };

        int16 Width, Height;    // Width and height in pixels
        FontFlags Flags;
        int16 Baseline;         // For underlined text
        ubyte MinChar, MaxChar; // The first and last chars defined by this font
        List<int16> Widths;     // Character widths for proportional fonts
        List<Kerning> Kernings; // Kernings for proportional fonts
        List<int> DataOffsets;  // offsets to bitmap data for each character
        List<ubyte> Data;       // bitmap data
        Palette Palette;

        int16 GetWidth(ubyte character) const {
            if (Flags & FT_PROPORTIONAL) {
                if (!Seq::inRange(Widths, character - MinChar)) return 0;
                return Widths[character - MinChar];
            }
            else {
                return Width;
            }
        }

        static Font Read(span<ubyte>);
    };

    enum class FontSize { Big, Medium, MediumGold, MediumBlue, Small };

    class FontAtlas {
    public:
        // character location on the atlas in UV coords
        struct Character {
            float X0, Y0; // top left UV
            float X1, Y1; // bottom right UV
        };

    private:
        const int _width, _height;
        int _x = 0, _y = 0;
        Array<List<Character>, 5> _lookup; // texture character position lookup
        Array<Font, 5> _fonts;

    public:
        FontAtlas(int width, int height) : _width(width), _height(height) {}

        const Character& GetCharacter(char c, FontSize font) const {
            c -= 32;
            if (c > _lookup[(int)font].size()) c = 0; // default to space if character is out of range
            return _lookup[(int)font][c];
        }

        const Font* GetFont(FontSize font) const {
            if (_lookup[(int)font].empty()) return nullptr;
            return &_fonts[(int)font];
        }

        int GetKerning(char c, char next, FontSize font) const;

        int Width() { return _width; }
        int Height() { return _height; }
        void AddFont(span<Palette::Color> dest, Font& font, FontSize fontSize, int padding = 1);
    };
}