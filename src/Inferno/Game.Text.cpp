#include "pch.h"
#include "Game.Text.h"
#include "FileSystem.h"
#include "Graphics/MaterialLibrary.h"
#include "Graphics/Render.h"
#include "Resources.h"

namespace Inferno {
    Vector2 MeasureString(string_view str, FontSize size) {
        float maxWidth = 0;
        float width = 0;
        auto font = Atlas.GetFont(size);
        if (!font) return {};

        float height = font->Height * font->Scale;

        for (int i = 0; i < str.size(); i++) {
            if (str[i] == '\n') {
                maxWidth = std::max(maxWidth, width);
                width = 0;
                height += font->Height * font->Scale + FONT_LINE_SPACING;
            }
            else {
                char next = i + 1 >= str.size() ? 0 : str[i + 1];
                auto kerning = Atlas.GetKerning(str[i], next, size);
                width += font->GetWidth(str[i]) * font->Scale + kerning;
            }
        }

        return { std::max(maxWidth, width), height };
    }

    string_view TrimStringByLength(string_view str, FontSize size, int maxLength) {
        float width = 0;
        auto font = Atlas.GetFont(size);
        if (!font) return {};

        for (int i = 0; i < str.size(); i++) {
            char next = i + 1 >= str.size() ? 0 : str[i + 1];
            auto kerning = Atlas.GetKerning(str[i], next, size);
            width += font->GetWidth(str[i]) * font->Scale + kerning;

            if (width > maxLength) {
                return i > 1 ? str.substr(0, i - 1) : string_view{};
            }
        }

        return str;
    }

    void LoadFonts() {
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
            //List<byte> data;
            float scale = 1;

            // Prefer reading high res fonts first
            //data = hog.TryReadEntry(name + "h.fnt");

            auto data = Resources::ReadBinaryFile(name + "h.fnt");

            if (data.empty()) {
                data = Resources::ReadBinaryFile(name + ".fnt");
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

        if (Atlas.FontCount() == 0) {
            SPDLOG_ERROR("No font data found");
            return;
        }

        Render::Adapter->WaitForGpu();
        auto batch = Render::BeginTextureUpload();
        Render::StaticTextures->Font.Load(batch, buffer.data(), Atlas.Width(), Atlas.Height(), L"Font");
        Render::StaticTextures->Font.AddShaderResourceView();
        Render::EndTextureUpload(batch, Render::Adapter->BatchUploadQueue->Get());
    }
}
