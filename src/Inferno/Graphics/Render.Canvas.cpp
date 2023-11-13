#include "pch.h"
#include "Render.Canvas.h"
#include "MaterialLibrary.h"

namespace Inferno::Render {
    Vector2 GetAlignment(const Vector2& size, AlignH alignH, AlignV alignV, const Vector2& screenSize) {
        Vector2 alignment;

        switch (alignH) {
            case Inferno::AlignH::Left: break; // no change
            case Inferno::AlignH::Center: alignment.x = screenSize.x / 2 - size.x / 2;
                break;
            case Inferno::AlignH::CenterLeft: alignment.x = screenSize.x / 2 - size.x;
                break;
            case Inferno::AlignH::CenterRight: alignment.x = screenSize.x / 2;
                break;
            case Inferno::AlignH::Right: alignment.x = screenSize.x - size.x;
                break;
        }

        switch (alignV) {
            case AlignV::Top: break; // no change
            case AlignV::Center: alignment.y = screenSize.y / 2 - size.y / 2;
                break;
            case AlignV::CenterTop: alignment.y = screenSize.y / 2;
                break;
            case AlignV::CenterBottom: alignment.y = screenSize.y / 2 - size.y;
                break;
            case AlignV::Bottom: alignment.y = screenSize.y - size.y;
                break;
        }

        return alignment;
    }

    void Canvas2D::DrawRectangle(const Vector2& pos, const Vector2& size, const Color& color) {
        CanvasPayload payload{};
        auto hex = color.BGRA();
        payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { 0, 1 }, hex }; // bottom left
        payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { 1, 1 }, hex }; // bottom right
        payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { 1, 0 }, hex }; // top right
        payload.V3 = { Vector2{ pos.x, pos.y }, { 0, 0 }, hex }; // top left
        payload.Texture = Materials->White().Handles[Material2D::Diffuse];
        Draw(payload);
    }

    void Canvas2D::DrawBitmap(TexID id, const Vector2& pos, const Vector2& size, const Color& color) {
        //auto material = &Materials->Get(id);
        auto handle = Materials->Get(id).Handles[Material2D::Diffuse];
        if (!handle.ptr)
            handle = Materials->White().Handles[Material2D::Diffuse];

        CanvasPayload payload{};
        auto hex = color.BGRA();
        payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { 0, 1 }, hex }; // bottom left
        payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { 1, 1 }, hex }; // bottom right
        payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { 1, 0 }, hex }; // top right
        payload.V3 = { Vector2{ pos.x, pos.y }, { 0, 0 }, hex }; // top left
        payload.Texture = handle;
        Draw(payload);
    }

    void Canvas2D::DrawBitmap(D3D12_GPU_DESCRIPTOR_HANDLE texture, const Vector2& pos, const Vector2& size, const Color& color) {
        CanvasPayload payload{};
        auto hex = color.BGRA();
        payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { 0, 1 }, hex }; // bottom left
        payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { 1, 1 }, hex }; // bottom right
        payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { 1, 0 }, hex }; // top right
        payload.V3 = { Vector2{ pos.x, pos.y }, { 0, 0 }, hex }; // top left
        payload.Texture = texture;
        Draw(payload);
    }

    void Canvas2D::DrawBitmap(D3D12_GPU_DESCRIPTOR_HANDLE texture, const Vector2& pos, const Vector2& size, const Vector2& uv0, const Vector2& uv1, const Color& color) {
        CanvasPayload payload{};
        auto hex = color.BGRA();
        payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { uv0.x, uv1.y }, hex }; // bottom left
        payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { uv1.x, uv1.y }, hex }; // bottom right
        payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { uv1.x, uv0.y }, hex }; // top right
        payload.V3 = { Vector2{ pos.x, pos.y }, { uv0.x, uv0.y }, hex }; // top left
        payload.Texture = texture;
        Draw(payload);
    }

    void Canvas2D::DrawBitmap(const CanvasBitmapInfo& info) {
        CanvasPayload payload{};
        auto hex = info.Color.BGRA();
        auto& pos = info.Position;
        auto size = info.Size;
        auto alignment = GetAlignment(size, info.HorizontalAlign, info.VerticalAlign, _size);
        auto uv0 = info.UV0;
        auto uv1 = info.UV1;
        if (info.MirrorX) std::swap(uv0.x, uv1.x);

        payload.V0 = { Vector2{ pos.x, pos.y + size.y } + alignment, { uv0.x, uv1.y }, hex }; // bottom left
        payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y } + alignment, uv1, hex }; // bottom right
        payload.V2 = { Vector2{ pos.x + size.x, pos.y } + alignment, { uv1.x, uv0.y }, hex }; // top right
        payload.V3 = { Vector2{ pos.x, pos.y } + alignment, uv0, hex }; // top left
        payload.Texture = info.Texture;
        Draw(payload);
    }

    void Canvas2D::Render(Graphics::GraphicsContext& ctx) {
        // draw batched text
        auto orthoProj = Matrix::CreateOrthographicOffCenter(0, _size.x, _size.y, 0.0, 0.0, -2.0f);

        auto cmdList = ctx.GetCommandList();
        ctx.ApplyEffect(*_effect);
        _effect->Shader->SetWorldViewProjection(cmdList, orthoProj);
        _effect->Shader->SetSampler(cmdList, Heaps->States.PointClamp());

        for (auto& group : _commands | views::values) {
            _effect->Shader->SetDiffuse(cmdList, group.front().Texture);
            _batch.Begin(cmdList);
            for (auto& c : group)
                _batch.DrawQuad(c.V0, c.V1, c.V2, c.V3);

            _batch.End();
            group.clear();
        }

        _commands.clear();
    }

    void Canvas2D::DrawGameText(string_view str,
                                float x, float y,
                                FontSize size, Color color,
                                float scale, AlignH alignH, AlignV alignV) {
        float xOffset = 0, yOffset = 0;
        auto font = Atlas.GetFont(size);
        if (!font) return;

        Color background = color * 0.1f;

        scale *= _scale;
        auto strSize = MeasureString(str, size) * scale;
        Vector2 alignment = GetAlignment(strSize, alignH, alignV, _size);
        bool inToken = false;

        for (int i = 0; i < str.size(); i++) {
            auto c = str[i];
            if (c == '\n') {
                xOffset = 0;
                yOffset += font->Height * scale * FONT_LINE_SPACING;
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
            auto x0 = alignment.x + xOffset + x;
            auto y0 = alignment.y + yOffset + y;

            Vector2 charSize = Vector2(font->GetWidth(c), font->Height) * scale;
            CanvasBitmapInfo info;
            info.Position = Vector2{ x0, y0 };
            info.Size = charSize;
            info.UV0 = Vector2{ ci.X0, ci.Y0 };
            info.UV1 = Vector2{ ci.X1, ci.Y1 };
            info.Color = background;
            info.Texture = Render::StaticTextures->Font.GetSRV();
            DrawBitmap(info); // Shadow

            info.Color = color;
            info.Position.x += 1;
            DrawBitmap(info); // Foreground

            auto kerning = Atlas.GetKerning(c, next, size) * scale;
            xOffset += charSize.x + kerning;
        }
    }

    void HudCanvas2D::DrawBitmap(const CanvasBitmapInfo& info) {
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
        Draw(payload);
    }

    void HudCanvas2D::Render(Graphics::GraphicsContext& ctx) {
        // draw batched text
        //auto orthoProj = Matrix::CreateOrthographicOffCenter(0, _size.x, _size.y, 0.0, 0.0, -2.0f);

        auto cmdList = ctx.GetCommandList();
        ctx.ApplyEffect(*_effect);

        HudShader::Constants constants;
        constants.Transform = Matrix::CreateOrthographicOffCenter(0, _size.x, _size.y, 0.0, 0.0, -2.0f);

        for (auto& group : _commands | views::values) {
            _effect->Shader->SetDiffuse(cmdList, group.front().Texture);
            _batch.Begin(cmdList);
            for (auto& g : group) {
                constants.ScanlinePitch = g.Scanline;
                _effect->Shader->SetConstants(ctx.GetCommandList(), constants);
                _batch.DrawQuad(g.V0, g.V1, g.V2, g.V3);
            }

            _batch.End();
            group.clear();
        }

        _commands.clear();
    }

    void HudCanvas2D::DrawGameText(string_view str, const DrawTextInfo& info) {
        float xOffset = 0, yOffset = 0;
        auto font = Atlas.GetFont(info.Font);
        if (!font) return;

        auto color = info.Color;
        Color background = color * 0.1f;

        auto scale = info.Scale * _scale;
        auto strSize = MeasureString(str, info.Font) * scale;
        Vector2 alignment = GetAlignment(strSize, info.HorizontalAlign, info.VerticalAlign, _size);
        bool inToken = false;

        for (int i = 0; i < str.size(); i++) {
            uchar c = str[i];
            if (c == '\n') {
                xOffset = 0;
                yOffset += font->Height * scale * FONT_LINE_SPACING;
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

            auto& ci = Atlas.GetCharacter(c, info.Font);
            auto x0 = alignment.x + xOffset + info.Position.x;
            auto y0 = alignment.y + yOffset + info.Position.y;

            //auto fontTex = Render::StaticTextures->Font.GetSRV();
            Vector2 charSize = Vector2(font->GetWidth(c), font->Height) * scale;
            //Vector2 uvMin = { ci.X0, ci.Y0 }, uvMax = { ci.X1, ci.Y1 };
            CanvasBitmapInfo cbi;
            cbi.Position = Vector2{ x0, y0 };
            cbi.Size = charSize;
            cbi.UV0 = Vector2{ ci.X0, ci.Y0 };
            cbi.UV1 = Vector2{ ci.X1, ci.Y1 };
            cbi.Color = background;
            cbi.Texture = Render::StaticTextures->Font.GetSRV();
            cbi.Scanline = info.Scanline;
            DrawBitmap(cbi); // Shadow

            cbi.Color = color;
            cbi.Position.x += 1;
            DrawBitmap(cbi); // Foreground

            auto kerning = Atlas.GetKerning(c, next, info.Font) * scale;
            xOffset += charSize.x + kerning;
        }
    }
}
