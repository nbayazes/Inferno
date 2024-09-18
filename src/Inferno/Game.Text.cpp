#include "pch.h"
#include "Game.Text.h"
#include "FileSystem.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"
#include "Resources.h"

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
                height += font->Height + FONT_LINE_SPACING;
            }
            else {
                char next = i + 1 >= str.size() ? 0 : str[i + 1];
                auto kerning = Atlas.GetKerning(str[i], next, size);
                width += font->GetWidth(str[i]) + kerning;
            }
        }

        return { std::max(maxWidth, width), height };
    }

    void LoadFonts() {
        // note: this does not search for loose font files
        // also this uses D2 fonts even for D1 if D2 is present
        auto hogPath = FileSystem::TryFindFile(L"descent2.hog");
        if (!hogPath) hogPath = FileSystem::TryFindFile(L"descent.hog");

        if (!hogPath) {
            SPDLOG_WARN("No hog file found for font data");
            return;
        }

        auto hog = HogFile::Read(*hogPath);

        Atlas = { 1024, 512 };
        List<Palette::Color> buffer(Atlas.Width() * Atlas.Height());
        ranges::fill(buffer, Palette::Color{ 0, 0, 0, 0 });

        // Ordered from small to large to simplify atlas packing.
        const Tuple<string, FontSize> fonts[] = {
            { "font3-1", FontSize::Small },
            { "font2-1", FontSize::Medium },
            { "font2-2", FontSize::MediumGold },
            { "font2-3", FontSize::MediumBlue },
            { "font1-1", FontSize::Big }
        };

        for (auto& [name, size] : fonts) {
            List<byte> data;
            float scale = 1;

            // Prefer reading high res fonts first
            data = hog.TryReadEntry(name + "h.fnt");

            if (data.empty()) {
                data = hog.TryReadEntry(name + ".fnt");
                scale = 2;
            }

            if (data.empty()) {
                //SPDLOG_WARN("Font data for {} not found", name);
                continue;
            }

            auto font = Font::Read(data);
            font.Scale = scale;
            Atlas.AddFont(buffer, font, size, 2);
        }

        Render::Adapter->WaitForGpu();
        auto batch = Render::BeginTextureUpload();
        Render::StaticTextures->Font.Load(batch, buffer.data(), Atlas.Width(), Atlas.Height(), L"Font");
        Render::StaticTextures->Font.AddShaderResourceView();
        Render::EndTextureUpload(batch, Render::Adapter->BatchUploadQueue->Get());
    }
}
