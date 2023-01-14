#include "pch.h"
#include "TextureEditor.h"

namespace Inferno::Editor {
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
}
