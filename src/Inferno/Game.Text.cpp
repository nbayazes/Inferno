#include "pch.h"
#include "Game.Text.h"
#include "FileSystem.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"
#include "HogFile.h"

namespace Inferno {
    FontAtlas Atlas(1024, 512);

    Vector2 MeasureString(string_view str, FontSize size) {
        float maxWidth = 0;
        float width = 0;
        auto font = Atlas.GetFont(size);
        if (!font) return {};

        float height = font->Height;

        for (int i = 0; i < str.size(); i++) {
            if (str[i] == '\n') {
                maxWidth = std::max(maxWidth, width);
                width = 0;
                height += font->Height * FONT_LINE_SPACING;
            }
            else {
                char next = i + 1 >= str.size() ? 0 : str[i + 1];
                auto kerning = Atlas.GetKerning(str[i], next, size);
                width += font->GetWidth(str[i]) + kerning;
            }
        }

        return { std::max(maxWidth, width), height };
    }

    bool LoadDescent2Fonts(span<Palette::Color> buffer) {
        auto hogPath = FileSystem::TryFindFile("descent2.hog");
        if (!hogPath) return false;

        auto hog = HogFile::Read(*hogPath);

        // Only load high res fonts. Ordered from small to large to simplify atlas code.
        const Tuple<string, FontSize> fonts[] = {
            { "font3-1h.fnt", FontSize::Small },
            { "font2-1h.fnt", FontSize::Medium },
            { "font2-2h.fnt", FontSize::MediumGold },
            { "font2-3h.fnt", FontSize::MediumBlue },
            { "font1-1h.fnt", FontSize::Big }
        };

        for (auto& [f, size] : fonts) {
            if (!hog.Exists(f)) continue;
            auto data = hog.ReadEntry(f);
            auto font = Font::Read(data);
            Atlas.AddFont(buffer, font, size, 2);
        }

        return true;
    }

    bool LoadDescent1Fonts(span<Palette::Color> buffer) {
        auto hogPath = FileSystem::TryFindFile("descent.hog");
        if (!hogPath) return false;

        auto hog = HogFile::Read(*hogPath);

        // Only load high res fonts. Ordered from small to large to simplify atlas code.
        const Tuple<string, FontSize> fonts[] = {
            { "font3-1.fnt", FontSize::Small },
            { "font2-1.fnt", FontSize::Medium },
            { "font2-2.fnt", FontSize::MediumGold },
            { "font2-3.fnt", FontSize::MediumBlue },
            { "font1-1.fnt", FontSize::Big }
        };

        for (auto& [f, size] : fonts) {
            if (!hog.Exists(f)) continue;
            auto data = hog.ReadEntry(f);
            auto font = Font::Read(data);
            Atlas.AddFont(buffer, font, size, 2);
        }

        Atlas.Scale = 2;
        return true;
    }

    void LoadFonts() {
        List<Palette::Color> buffer(Atlas.Width() * Atlas.Height());
        ranges::fill(buffer, Palette::Color{ 0, 0, 0, 0 });

        // Prefer fonts from the d2 hog file as they are higher resolution
        if (!LoadDescent2Fonts(buffer) && !LoadDescent1Fonts(buffer))
            return;

        auto batch = Render::BeginTextureUpload();
        Render::StaticTextures->Font.Load(batch, buffer.data(), Atlas.Width(), Atlas.Height(), L"Font");
        Render::StaticTextures->Font.AddShaderResourceView();
        Render::EndTextureUpload(batch, Render::Adapter->BatchUploadQueue->Get());
    }
}
