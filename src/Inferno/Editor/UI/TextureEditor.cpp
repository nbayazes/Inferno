#include "pch.h"
#include "TextureEditor.h"

namespace Inferno::Editor {
    void WriteBmp(const filesystem::path& path, const Palette& gamePalette, const PigBitmap& bmp) {
        std::ofstream stream(path, std::ios::binary);
        StreamWriter writer(stream, false);

        constexpr DWORD offset = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 256 * 4;
        constexpr int bpp = 8;
        const int padding = (bmp.Info.Width * bpp + 31) / 32 * 4 - bmp.Info.Width;

        BITMAPFILEHEADER bmfh{
            .bfType = 'MB',
            .bfSize = offset + (bmp.Info.Width + padding) * bmp.Info.Height,
            .bfOffBits = offset
        };

        BITMAPINFOHEADER bmih{
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = bmp.Info.Width,
            .biHeight = -bmp.Info.Height, // Top down
            .biPlanes = 1,
            .biBitCount = bpp,
            .biCompression = BI_RGB,
            .biSizeImage = 0,
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

        for (size_t i = 0; i < bmp.Info.Height; i++) {
            for (size_t j = 0; j < bmp.Info.Width; j++) {
                writer.Write<ubyte>(bmp.Indexed[i * bmp.Info.Width + j]);
            }
            for (size_t j = 0; j < padding; j++) {
                writer.Write<ubyte>(0); // pad rows of data to an alignment of 4
            }
        }
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
