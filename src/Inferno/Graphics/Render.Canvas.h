#pragma once

#include "Types.h"
#include "DirectX.h"
#include "ShaderLibrary.h"
#include "DeviceResources.h"
#include "CommandContext.h"
#include "Game.Text.h"
#include "Shell.h"

namespace Inferno::Render {

    struct StaticTextureDef {
        Texture2D Font;
        Texture2D ImguiFont;
    };

    inline Ptr<StaticTextureDef> StaticTextures;

    struct DrawTextInfo {
        Vector2 Position;
        FontSize Font = FontSize::Small;
        float Scale = 1;
        Color Color = { 1, 1, 1 };
        AlignH HorizontalAlign = AlignH::Left;
        AlignV VerticalAlign = AlignV::Top;
        float Scanline = 0;
    };

    struct CanvasPayload {
        CanvasVertex V0, V1, V2, V3;
        D3D12_GPU_DESCRIPTOR_HANDLE Texture{};
        float Scanline = 0;
    };

    extern Ptr<DeviceResources> Adapter;
    extern Ptr<ShaderResources> Shaders;
    extern Ptr<EffectResources> Effects;

    struct CanvasBitmapInfo {
        Vector2 Position, Size;
        D3D12_GPU_DESCRIPTOR_HANDLE Texture;
        Color Color = { 1, 1, 1 };
        AlignH HorizontalAlign = AlignH::Left;
        AlignV VerticalAlign = AlignV::Top;
        Vector2 UV0{ 0, 0 }, UV1{ 1, 1 };
        float Scanline = 0;
    };


    inline Vector2 GetAlignment(const Vector2& size, AlignH alignH, AlignV alignV, const Vector2& screenSize) {
        Vector2 alignment;

        switch (alignH) {
            case Inferno::AlignH::Left: break; // no change
            case Inferno::AlignH::Center: alignment.x = screenSize.x / 2 - size.x / 2; break;
            case Inferno::AlignH::CenterLeft: alignment.x = screenSize.x / 2 - size.x; break;
            case Inferno::AlignH::CenterRight: alignment.x = screenSize.x / 2; break;
            case Inferno::AlignH::Right: alignment.x = screenSize.x - size.x; break;
        }

        switch (alignV) {
            case AlignV::Top: break; // no change
            case AlignV::Center: alignment.y = screenSize.y / 2 - size.y / 2; break;
            case AlignV::CenterTop: alignment.y = screenSize.y / 2; break;
            case AlignV::CenterBottom: alignment.y = screenSize.y / 2 - size.y; break;
            case AlignV::Bottom: alignment.y = screenSize.y - size.y; break;
        }

        return alignment;
    }

    // Draws a quad to the 2D canvas (UI Layer)
    template <class TShader>
    class Canvas2D {
        DirectX::PrimitiveBatch<CanvasVertex> _batch;
        Dictionary<uint64, List<CanvasPayload>> _commands;
        Effect<TShader>& _effect;
        Vector2 _size = { 1024, 1024 };
        float _scale = 1;
    public:
        Canvas2D(ID3D12Device* device, Effect<TShader>& effect) : _batch(device), _effect(effect) {}

        // Sets the size of the canvas. Affects alignment.
        void SetSize(uint width, uint height, uint targetScreenHeight = 480) {
            _size = { (float)width, (float)height };
            _scale = (float)height / targetScreenHeight; // scaling due to original screen size being 480 pixels
        }

        float GetScale() { return _scale; }

        void Draw(const CanvasPayload& payload) {
            if (!payload.Texture.ptr) return;
            _commands[payload.Texture.ptr].push_back(payload);
        }

        void DrawBitmap(TexID id, const Vector2& pos, const Vector2& size, const Color& color = { 1, 1, 1 }) {
            //auto material = &Materials->Get(id);
            auto handle = Materials->Get(id).Handles[Material2D::Diffuse];
            if (!handle.ptr)
                handle = Materials->White.Handles[Material2D::Diffuse];

            CanvasPayload payload{};
            auto hex = color.BGRA();
            payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { 0, 1 }, hex }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { 1, 1 }, hex }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { 1, 0 }, hex }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y }, { 0, 0 }, hex }; // top left
            payload.Texture = handle;
            Draw(payload);
        }

        void DrawBitmap(D3D12_GPU_DESCRIPTOR_HANDLE texture, const Vector2& pos, const Vector2& size, const Color& color = { 1, 1, 1 }) {
            CanvasPayload payload{};
            auto hex = color.BGRA();
            payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { 0, 1 }, hex }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { 1, 1 }, hex }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { 1, 0 }, hex }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y }, { 0, 0 }, hex }; // top left
            payload.Texture = texture;
            Draw(payload);
        }

        void DrawBitmap(D3D12_GPU_DESCRIPTOR_HANDLE texture, const Vector2& pos, const Vector2& size, const Vector2& uv0, const Vector2& uv1, const Color& color = { 1, 1, 1 }) {
            CanvasPayload payload{};
            auto hex = color.BGRA();
            payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { uv0.x, uv1.y }, hex }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { uv1.x, uv1.y }, hex }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { uv1.x, uv0.y }, hex }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y }, { uv0.x, uv0.y }, hex }; // top left
            payload.Texture = texture;
            Draw(payload);
        }

        void DrawBitmap(const CanvasBitmapInfo& info) {
            CanvasPayload payload{};
            auto hex = info.Color.BGRA();
            auto& pos = info.Position;
            auto size = info.Size;
            auto alignment = GetAlignment(size, info.HorizontalAlign, info.VerticalAlign, _size);
            auto& uv0 = info.UV0;
            auto& uv1 = info.UV1;
            payload.V0 = { Vector2{ pos.x, pos.y + size.y } + alignment, { uv0.x, uv1.y }, hex }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y } + alignment, uv1, hex }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y } + alignment, { uv1.x, uv0.y }, hex }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y } + alignment, uv0, hex }; // top left
            payload.Texture = info.Texture;
            Draw(payload);
        }

        void Render(Graphics::GraphicsContext& ctx) {
            // draw batched text
            auto orthoProj = Matrix::CreateOrthographicOffCenter(0, _size.x, _size.y, 0.0, 0.0, -2.0f);

            auto cmdList = ctx.CommandList();
            ctx.ApplyEffect(_effect);
            _effect.Shader->SetWorldViewProjection(cmdList, orthoProj);

            for (auto& [texture, group] : _commands) {
                _effect.Shader->SetDiffuse(cmdList, group.front().Texture);
                _batch.Begin(cmdList);
                for (auto& c : group)
                    _batch.DrawQuad(c.V0, c.V1, c.V2, c.V3);

                _batch.End();
                group.clear();
            }

            _commands.clear();
        }

        void DrawGameText(string_view str,
                          float x, float y,
                          FontSize size,
                          Color color,
                          float scale = 1,
                          AlignH alignH = AlignH::Left, AlignV alignV = AlignV::Top) {
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

                auto fontTex = Render::StaticTextures->Font.GetSRV();
                Vector2 charSize = Vector2((float)font->GetWidth(c), (float)font->Height) * scale;
                Vector2 uvMin = { ci.X0, ci.Y0 }, uvMax = { ci.X1, ci.Y1 };
                CanvasBitmapInfo info;
                info.Position = { x0, y0 };
                info.Size = charSize;
                info.UV0 = { ci.X0, ci.Y0 };
                info.UV1 = { ci.X1, ci.Y1 };
                info.Color = background;
                info.Texture = Render::StaticTextures->Font.GetSRV();
                DrawBitmap(info); // Shadow

                info.Color = color;
                info.Position.x += 1;
                DrawBitmap(info); // Foreground
                //DrawBitmap(fontTex, Vector2{ x0, y0 }, charSize, uvMin, uvMax, background);
                //DrawBitmap(fontTex, Vector2{ x0 + 1, y0 }, charSize, uvMin, uvMax, color);

                auto kerning = Atlas.GetKerning(c, next, size) * scale;
                xOffset += charSize.x + kerning;
            }
        }
    };

    class HudCanvas2D {
        DirectX::PrimitiveBatch<CanvasVertex> _batch;
        Dictionary<uint64, List<CanvasPayload>> _commands;
        Effect<HudShader>& _effect;
        Vector2 _size = { 1024, 1024 };
        float _scale = 1;
    public:
        HudCanvas2D(ID3D12Device* device, Effect<HudShader>& effect) : _batch(device), _effect(effect) {}

        // Sets the size of the canvas. Affects alignment.
        void SetSize(uint width, uint height, uint targetScreenHeight = 480) {
            _size = { (float)width, (float)height };
            _scale = (float)height / targetScreenHeight; // scaling due to original screen size being 480 pixels
        }

        float GetScale() { return _scale; }

        void DrawBitmap(const CanvasBitmapInfo& info) {
            CanvasPayload payload{};
            auto hex = info.Color.RGBA().v;
            auto& pos = info.Position;
            auto size = info.Size;
            auto alignment = GetAlignment(size, info.HorizontalAlign, info.VerticalAlign, _size);
            auto& uv0 = info.UV0;
            auto& uv1 = info.UV1;
            payload.V0 = { Vector2{ pos.x, pos.y + size.y } + alignment, { uv0.x, uv1.y }, hex }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y } + alignment, uv1, hex }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y } + alignment, { uv1.x, uv0.y }, hex }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y } + alignment, uv0, hex }; // top left
            payload.Texture = info.Texture;
            payload.Scanline = info.Scanline;
            Draw(payload);
        }

        void Render(Graphics::GraphicsContext& ctx) {
            // draw batched text
            //auto orthoProj = Matrix::CreateOrthographicOffCenter(0, _size.x, _size.y, 0.0, 0.0, -2.0f);

            auto cmdList = ctx.CommandList();
            ctx.ApplyEffect(_effect);
            
            HudShader::Constants constants;
            constants.Transform = Matrix::CreateOrthographicOffCenter(0, _size.x, _size.y, 0.0, 0.0, -2.0f);

            for (auto& [texture, group] : _commands) {
                _effect.Shader->SetDiffuse(cmdList, group.front().Texture);
                _batch.Begin(cmdList);
                for (auto& g : group) {
                    constants.ScanlinePitch = g.Scanline;
                    _effect.Shader->SetConstants(ctx.CommandList(), constants);
                    _batch.DrawQuad(g.V0, g.V1, g.V2, g.V3);
                }

                _batch.End();
                group.clear();
            }

            _commands.clear();
        }

        void DrawGameText(string_view str, const DrawTextInfo& info) {
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

                auto& ci = Atlas.GetCharacter(c, info.Font);
                auto x0 = alignment.x + xOffset + info.Position.x;
                auto y0 = alignment.y + yOffset + info.Position.y;

                //auto fontTex = Render::StaticTextures->Font.GetSRV();
                Vector2 charSize = Vector2((float)font->GetWidth(c), (float)font->Height) * scale;
                //Vector2 uvMin = { ci.X0, ci.Y0 }, uvMax = { ci.X1, ci.Y1 };
                CanvasBitmapInfo cbi;
                cbi.Position = { x0, y0 };
                cbi.Size = charSize;
                cbi.UV0 = { ci.X0, ci.Y0 };
                cbi.UV1 = { ci.X1, ci.Y1 };
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

    private:
        void Draw(const CanvasPayload& payload) {
            if (!payload.Texture.ptr) return;
            _commands[payload.Texture.ptr].push_back(payload);
        }
    };
}