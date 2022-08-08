#include "pch.h"
#include "Game.Text.h"
#include "FileSystem.h"
#include "HogFile.h"
#include "Graphics/Render.h"
#include "Shell.h"

namespace Inferno {
    FontAtlas Atlas(1024, 512);

    Vector2 MeasureString(string_view str, FontSize size) {
        float width = 0;
        auto font = Atlas.GetFont(size);
        if (!font) return {};

        for (int i = 0; i < str.size(); i++) {
            char next = i + 1 >= str.size() ? 0 : str[i + 1];
            auto kerning = Atlas.GetKerning(str[i], next, size);
            width += font->GetWidth(str[i]) + kerning;
        }

        return { width, (float)font->Height };
    }

    // Loads fonts from the d2 hog file as they are higher resolution
    void LoadFonts() {
        auto hogPath = FileSystem::TryFindFile("descent2.hog");
        if (!hogPath) return;

        auto hog = HogFile::Read(*hogPath);

        // Only load high res fonts. Ordered from small to large to simplify atlas code.
        const Tuple<string, FontSize> fonts[] = {
            { "font3-1h.fnt", FontSize::Small },
            { "font2-1h.fnt", FontSize::Medium },
            { "font2-2h.fnt", FontSize::MediumGold },
            { "font2-3h.fnt", FontSize::MediumBlue },
            { "font1-1h.fnt", FontSize::Big }
        };

        List<Palette::Color> buffer(Atlas.Width() * Atlas.Height());
        std::fill(buffer.begin(), buffer.end(), Palette::Color{ 0, 0, 0, 0 });

        for (auto& [f, sz] : fonts) {
            if (!hog.Exists(f)) continue;
            auto data = hog.ReadEntry(f);
            auto font = Font::Read(data);
            Atlas.AddFont(buffer, font, sz, 2);
        }

        auto batch = Render::BeginTextureUpload();
        Render::StaticTextures->Font.Load(batch, buffer.data(), Atlas.Width(), Atlas.Height(), L"Font");
        Render::StaticTextures->Font.AddShaderResourceView();
        Render::EndTextureUpload(batch);
    }

    void DrawGameText(string_view str,
                      Render::Canvas2D& canvas,
                      const RenderTarget& target,
                      float x, float y,
                      FontSize size,
                      Color color,
                      AlignH alignH, AlignV alignV) {
        float xOffset = 0, yOffset = 0;
        auto font = Atlas.GetFont(size);
        if (!font) return;

        Color background = color * 0.1f;

        Vector2 alignment;

        {
            auto width = (float)target.GetWidth();
            auto height = (float)target.GetHeight();
            auto [strWidth, strHeight] = MeasureString(str, size);

            if (alignH == AlignH::Center) {
                // shift string center to screen center
                alignment.x = width / 2 - strWidth / 2;
            }
            else if (alignH == AlignH::Right) {
                alignment.x = width - strWidth;
            }

            if (alignV == AlignV::Center) {
                // shift string center to screen center
                alignment.y = height / 2 - strHeight / 2;
            }
            else if (alignV == AlignV::Bottom) {
                alignment.y = height - strHeight;
            }
        }

        bool inToken = false;

        for (int i = 0; i < str.size(); i++) {
            auto c = str[i];
            if (c == '\n') {
                xOffset = 0;
                yOffset += font->Height * 1.5f * Shell::DpiScale;
                continue;
            }


            char next = i + 1 >= str.size() ? 0 : str[i + 1];

            if (c == '$') {
                inToken = true;
                continue;
            }

            if (inToken) {
                if (c == 'C') {
                    if (next == '1') {
                        color = ColorFromRGB(0, 219, 0);
                        background = ColorFromRGB(0, 75, 0);
                    }
                    else if (next == '2') {
                        color = ColorFromRGB(163, 151, 147);
                        background = ColorFromRGB(19, 19, 27);
                        
                    }
                    else if (next == '3') {
                        color = ColorFromRGB(100, 109, 117);
                        background = ColorFromRGB(19, 19, 27);
                    }
                }

                i++;
                inToken = false;
                continue;
            }

            auto& ci = Atlas.GetCharacter(c, size);
            auto width = font->GetWidth(c) * Shell::DpiScale;
            auto x0 = alignment.x + xOffset + x;
            auto x1 = alignment.x + xOffset + x + width;
            auto y0 = alignment.y + yOffset + y;
            auto y1 = alignment.y + yOffset + y + font->Height * Shell::DpiScale;

            Render::CanvasPayload bg{};
            bg.V0 = { Vector2{ x0, y1 },{ ci.X0, ci.Y1 }, background.BGRA() }; // bottom left
            bg.V1 = { Vector2{ x1, y1 },{ ci.X1, ci.Y1 }, background.BGRA() }; // bottom right
            bg.V2 = { Vector2{ x1, y0 },{ ci.X1, ci.Y0 }, background.BGRA() }; // top right
            bg.V3 = { Vector2{ x0, y0 },{ ci.X0, ci.Y0 }, background.BGRA() }; // top left
            bg.Texture = Render::StaticTextures->Font.GetSRV();
            canvas.Draw(bg);

            Render::CanvasPayload payload{};
            payload.V0 = { Vector2{ x0 + 1, y1 },{ ci.X0, ci.Y1 }, color.BGRA() }; // bottom left
            payload.V1 = { Vector2{ x1 + 1, y1 },{ ci.X1, ci.Y1 }, color.BGRA() }; // bottom right
            payload.V2 = { Vector2{ x1 + 1, y0 },{ ci.X1, ci.Y0 }, color.BGRA() }; // top right
            payload.V3 = { Vector2{ x0 + 1, y0 },{ ci.X0, ci.Y0 }, color.BGRA() }; // top left
            payload.Texture = Render::StaticTextures->Font.GetSRV();
            canvas.Draw(payload);

            auto kerning = Atlas.GetKerning(c, next, size);
            xOffset += width + kerning;
        }
    }
}