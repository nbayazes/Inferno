#include "pch.h"
#include "Render.Canvas.h"
#include "Render.h"

namespace Inferno::Render {
    Vector2 GetAlignment(const Vector2& size, AlignH alignH, AlignV alignV, const Vector2& parentSize, const Vector2& margin) {
        Vector2 alignment;

        switch (alignH) {
            case Inferno::AlignH::Left: 
                break; // no change
            case Inferno::AlignH::Center: 
                alignment.x = parentSize.x / 2 - size.x / 2;
                break;
            case Inferno::AlignH::CenterLeft: 
                alignment.x = parentSize.x / 2 - size.x;
                break;
            case Inferno::AlignH::CenterRight: 
                alignment.x = parentSize.x / 2;
                break;
            case Inferno::AlignH::Right:
                alignment.x = parentSize.x - size.x - margin.x * 2;
                break;
        }

        switch (alignV) {
            case AlignV::Top:
                break; // no change
            case AlignV::Center: 
                alignment.y = parentSize.y / 2 - size.y / 2;
                break;
            case AlignV::CenterTop: 
                alignment.y = parentSize.y / 2;
                break;
            case AlignV::CenterBottom: 
                alignment.y = parentSize.y / 2 - size.y;
                break;
            case AlignV::Bottom: 
                alignment.y = parentSize.y - size.y - margin.y * 2;
                break;
        }

        return alignment;
    }

    void HudCanvas2D::DrawBitmap(const CanvasBitmapInfo& info, int layer) {
        HudCanvasPayload payload{};
        auto& pos = info.Position;
        auto size = info.Size;
        auto alignment = GetAlignment(size, info.HorizontalAlign, info.VerticalAlign, _size);
        auto uv0 = info.UV0;
        auto uv1 = info.UV1;
        if (info.MirrorX) std::swap(uv0.x, uv1.x);

        payload.V0 = { Vector2{ pos.x, pos.y + size.y } + alignment, { uv0.x, uv1.y }, info.Color }; // bottom left
        payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y } + alignment, uv1, info.Color }; // bottom right
        payload.V2 = { Vector2{ pos.x + size.x, pos.y } + alignment, { uv1.x, uv0.y }, info.Color }; // top right
        payload.V3 = { Vector2{ pos.x, pos.y } + alignment, uv0, info.Color }; // top left
        payload.Texture = info.Texture;
        payload.Scanline = info.Scanline;
        payload.Layer = layer;
        Draw(payload);
    }

    void HudCanvas2D::DrawBitmapScaled(const CanvasBitmapInfo& info, int layer) {
        HudCanvasPayload payload{};
        auto pos = info.Position * _scale;
        auto size = info.Size * _scale;
        auto alignment = GetAlignment(size, info.HorizontalAlign, info.VerticalAlign, _size);
        auto uv0 = info.UV0;
        auto uv1 = info.UV1;
        if (info.MirrorX) std::swap(uv0.x, uv1.x);

        payload.V0 = { Vector2{ pos.x, pos.y + size.y } + alignment, { uv0.x, uv1.y }, info.Color }; // bottom left
        payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y } + alignment, uv1, info.Color }; // bottom right
        payload.V2 = { Vector2{ pos.x + size.x, pos.y } + alignment, { uv1.x, uv0.y }, info.Color }; // top right
        payload.V3 = { Vector2{ pos.x, pos.y } + alignment, uv0, info.Color }; // top left
        payload.Texture = info.Texture;
        payload.Scanline = info.Scanline;
        payload.Layer = layer;
        Draw(payload);
    }

    void HudCanvas2D::Render(GraphicsContext& ctx) {
        // draw batched text
        //auto orthoProj = Matrix::CreateOrthographicOffCenter(0, _size.x, _size.y, 0.0, 0.0, -2.0f);

        auto cmdList = ctx.GetCommandList();
        ctx.ApplyEffect(*_effect);

        ctx.SetConstantBuffer(0, Adapter->GetFrameConstants().GetGPUVirtualAddress());

        HudShader::Constants constants;
        constants.Transform = Matrix::CreateOrthographicOffCenter(0, _size.x, _size.y, 0.0, 0.0, -2.0f);

        for (auto& layer : _commands) {
            for (auto& group : layer | views::values) {
                _effect->Shader->SetDiffuse(cmdList, group.front().Texture);
                _batch.Begin(cmdList);
                for (auto& g : group) {
                    constants.Scanline = g.Scanline;
                    _effect->Shader->SetConstants(ctx.GetCommandList(), constants);
                    _batch.DrawQuad(g.V0, g.V1, g.V2, g.V3);
                }

                _batch.End();
                group.clear();
            }

            layer.clear();
        }
    }

    void HudCanvas2D::DrawText(string_view str, const DrawTextInfo& info, int layer) {
        float xOffset = 0, yOffset = 0;
        auto font = Atlas.GetFont(info.Font);
        if (!font) return;

        auto color = info.Color;
        Color background = color * 0.1f;
        background.w = 1;

        auto scale = info.Scale * _scale;
        auto strSize = MeasureString(str, info.Font) * scale;
        Vector2 alignment = GetAlignment(strSize, info.HorizontalAlign, info.VerticalAlign, _size);
        bool inToken = false;

        for (int i = 0; i < str.size(); i++) {
            uchar c = str[i];
            if (c == '\n') {
                xOffset = 0;
                yOffset += (font->Height * font->Scale + FONT_LINE_SPACING) * scale;
                continue;
            }

            char next = i + 1 >= str.size() ? 0 : str[i + 1];

            if (info.EnableTokenParsing) {
                if (c == '$') {
                    inToken = true;
                    continue;
                }

                if (c == '\t') {
                    xOffset = info.TabStop * scale;
                    continue;
                }

                if (c == ';') {
                    break; // skip comments
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
            }

            auto& ci = Atlas.GetCharacter(c, info.Font);
            auto x0 = alignment.x + xOffset + info.Position.x * _scale; // do not scale position by font scale
            auto y0 = alignment.y + yOffset + info.Position.y * _scale; // do not scale position by font scale

            //if (info.IntegerScaling) 
            //    scale = std::round(scale * 2) / 2;

            Vector2 charSize = Vector2(font->GetWidth(c) * font->Scale, font->Height * font->Scale) * scale;
            CanvasBitmapInfo cbi;
            cbi.Position = Vector2{ x0 - 1 * scale, y0 + 1 * scale };
            //cbi.Position.x = std::round(cbi.Position.x);
            //cbi.Position.y = std::round(cbi.Position.y);
            cbi.Size = charSize;
            //cbi.Size.x = std::round(cbi.Size.x);
            //cbi.Size.y = std::round(cbi.Size.y);
            cbi.UV0 = Vector2{ ci.X0, ci.Y0 };
            cbi.UV1 = Vector2{ ci.X1, ci.Y1 };
            cbi.Color = background;
            cbi.Texture = Render::StaticTextures->Font.GetSRV();
            DrawBitmap(cbi, layer); // Shadow

            cbi.Color = color;
            cbi.Position = Vector2{ x0, y0 };
            cbi.Scanline = info.Scanline;
            DrawBitmap(cbi, layer); // Foreground

            auto kerning = Atlas.GetKerning(c, next, info.Font) * scale;
            xOffset += charSize.x + kerning;
        }
    }

    void HudCanvas2D::DrawRaw(string_view str, const DrawTextInfo& info, int layer) {
        float xOffset = 0, yOffset = 0;
        auto font = Atlas.GetFont(info.Font);
        if (!font) return;

        auto color = info.Color;
        Color background = color * 0.1f;
        background.w = 1;

        auto canvasScale = info.IntegerScaling ? std::round(_scale) : _scale;
        auto scale = info.Scale * canvasScale;
        auto strSize = MeasureString(str, info.Font) * canvasScale;
        Vector2 alignment = GetAlignment(strSize, info.HorizontalAlign, info.VerticalAlign, _size);
        bool inToken = false;
        auto position = info.Position;
        position.x = std::round(position.x);
        position.y = std::round(position.x);

        for (int i = 0; i < str.size(); i++) {
            uchar c = str[i];
            if (c == '\n') {
                xOffset = 0;
                yOffset += (font->Height * font->Scale + FONT_LINE_SPACING) * scale;
                continue;
            }

            char next = i + 1 >= str.size() ? 0 : str[i + 1];

            if (info.EnableTokenParsing) {
                if (c == '$') {
                    inToken = true;
                    continue;
                }

                if (c == '\t') {
                    xOffset = info.TabStop * scale;
                    continue;
                }

                if (c == ';') {
                    break; // skip comments
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
            }

            auto& ci = Atlas.GetCharacter(c, info.Font);
            auto x0 = alignment.x + xOffset + info.Position.x;
            auto y0 = alignment.y + yOffset + info.Position.y;

            //if (info.IntegerScaling) 
            //    scale = std::round(scale * 2) / 2;

            Vector2 charSize = Vector2(font->GetWidth(c) * font->Scale, font->Height * font->Scale) * scale;
            CanvasBitmapInfo cbi;
            cbi.Position = Vector2{ x0 - 1 * scale, y0 + 1 * scale };
            cbi.Size = charSize;
            cbi.UV0 = Vector2{ ci.X0, ci.Y0 };
            cbi.UV1 = Vector2{ ci.X1, ci.Y1 };
            cbi.Color = background;
            cbi.Texture = Render::StaticTextures->Font.GetSRV();
            DrawBitmap(cbi, layer); // Shadow

            cbi.Color = color;
            cbi.Position = Vector2{ x0, y0 };
            cbi.Scanline = info.Scanline;
            DrawBitmap(cbi, layer); // Foreground

            auto kerning = Atlas.GetKerning(c, next, info.Font) * scale;
            xOffset += charSize.x + kerning;
        }
    }
}
