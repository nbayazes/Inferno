#pragma once

#include "Types.h"
#include "DirectX.h"
#include "ShaderLibrary.h"
#include "Game.h"
#include "Game.Text.h"
#include "MaterialLibrary.h"

namespace Inferno::Render {
    Vector2 GetAlignment(const Vector2& size, AlignH alignH, AlignV alignV, const Vector2& parentSize, const Vector2& margin = Vector2::Zero);

    struct StaticTextureDef {
        Texture2D Font;
        Texture2D ImguiFont;
        Texture2D Missing; // Purple checkerboard
        Texture2D Normal; // Flat normal texture
        Texture2D Black;
        Texture2D White;
    };

    inline Ptr<StaticTextureDef> StaticTextures;

    struct DrawTextInfo {
        Vector2 Position; // Positive Y is down
        FontSize Font = FontSize::Small;
        float Scale = 1;
        Color Color = { 1, 1, 1 };
        AlignH HorizontalAlign = AlignH::Left;
        AlignV VerticalAlign = AlignV::Top;
        float Scanline = 0; // There is a bug that causes scanlines to not update per-draw
        float TabStop = 0;
        bool IntegerScaling = false;
    };

    struct CanvasPayload {
        CanvasVertex V0, V1, V2, V3;
        D3D12_GPU_DESCRIPTOR_HANDLE Texture{};
        int Layer = 0;
        float Scanline = 0;
    };

    struct CanvasBitmapInfo {
        Vector2 Position, Size;
        D3D12_GPU_DESCRIPTOR_HANDLE Texture;
        Color Color = { 1, 1, 1 };
        AlignH HorizontalAlign = AlignH::Left;
        AlignV VerticalAlign = AlignV::Top;
        Vector2 UV0{ 0, 0 }, UV1{ 1, 1 };
        float Scanline = 0;
        bool MirrorX = false;
    };

    constexpr uint CANVAS_HEIGHT = 480;

    // Draws a quad to the 2D canvas (UI Layer)
    template <class TShader>
    class Canvas2D {
        DirectX::PrimitiveBatch<CanvasVertex> _batch;
        Effect<TShader>* _effect;
        Vector2 _size = { 1024, 1024 };
        float _scale = 1;

        // Layers -> Texture id -> commands
        Array<Dictionary<uint64, List<CanvasPayload>>, 10> _commands;

    public:
        Canvas2D(ID3D12Device* device, Effect<TShader>& effect) : _batch(device), _effect(&effect) {}

        // Sets the size of the canvas. Affects alignment. Target screen height is the original resolution.
        void SetSize(uint width, uint height, uint targetScreenHeight = CANVAS_HEIGHT) {
            _size = Vector2{ (float)width, (float)height };
            _scale = (float)height / targetScreenHeight; // scaling due to original screen size being 480 pixels
        }

        const Vector2& GetSize() const { return _size; }
        float GetScale() const { return _scale; }

        void Draw(const CanvasPayload& payload) {
            if (!payload.Texture.ptr) return;
            auto layer = std::clamp(payload.Layer, 0, (int)_commands.size() - 1);
            _commands[layer][payload.Texture.ptr].push_back(payload);
        }

        void DrawRectangle(const Vector2& pos, const Vector2& size, const Color& color, int layer = 0) {
            CanvasPayload payload{};
            auto hex = color.RGBA().v;
            payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { 0, 1 }, hex }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { 1, 1 }, hex }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { 1, 0 }, hex }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y }, { 0, 0 }, hex }; // top left
            payload.Texture = Materials->White().Handles[Material2D::Diffuse];
            payload.Layer = layer;
            Draw(payload);
        }

        void DrawBitmap(TexID id, const Vector2& pos, const Vector2& size, const Color& color = { 1, 1, 1 }) {
            auto handle = Materials->Get(id).Handles[Material2D::Diffuse];
            if (!handle.ptr)
                handle = Materials->White().Handles[Material2D::Diffuse];

            CanvasPayload payload{};
            auto hex = color.RGBA().v;
            payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { 0, 1 }, hex }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { 1, 1 }, hex }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { 1, 0 }, hex }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y }, { 0, 0 }, hex }; // top left
            payload.Texture = handle;
            Draw(payload);
        }

        void DrawBitmap(D3D12_GPU_DESCRIPTOR_HANDLE texture, const Vector2& pos, const Vector2& size, const Color& color = { 1, 1, 1 }, int layer = 0) {
            CanvasPayload payload{};
            auto hex = color.RGBA().v;
            payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { 0, 1 }, hex }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { 1, 1 }, hex }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { 1, 0 }, hex }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y }, { 0, 0 }, hex }; // top left
            payload.Texture = texture;
            payload.Layer = layer;
            Draw(payload);
        }

        void DrawBitmap(D3D12_GPU_DESCRIPTOR_HANDLE texture, const Vector2& pos, const Vector2& size, const Vector2& uv0, const Vector2& uv1, const Color& color = { 1, 1, 1 }) {
            CanvasPayload payload{};
            auto hex = color.RGBA().v;
            payload.V0 = { Vector2{ pos.x, pos.y + size.y }, { uv0.x, uv1.y }, hex }; // bottom left
            payload.V1 = { Vector2{ pos.x + size.x, pos.y + size.y }, { uv1.x, uv1.y }, hex }; // bottom right
            payload.V2 = { Vector2{ pos.x + size.x, pos.y }, { uv1.x, uv0.y }, hex }; // top right
            payload.V3 = { Vector2{ pos.x, pos.y }, { uv0.x, uv0.y }, hex }; // top left
            payload.Texture = texture;
            Draw(payload);
        }

        void DrawBitmap(const CanvasBitmapInfo& info, int layer = 0) {
            CanvasPayload payload{};
            auto hex = info.Color.RGBA().v;
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
            payload.Layer = layer;
            Draw(payload);
        }

        void Render(GraphicsContext& ctx) {
            auto orthoProj = Matrix::CreateOrthographicOffCenter(0, _size.x, _size.y, 0.0, 0.0, -2.0f);

            auto cmdList = ctx.GetCommandList();
            ctx.ApplyEffect(*_effect);
            _effect->Shader->SetWorldViewProjection(cmdList, orthoProj);
            _effect->Shader->SetSampler(cmdList, Heaps->States.PointClamp());

            for (auto& layer : _commands) {
                for (auto& group : layer | views::values) {
                    _batch.Begin(cmdList);
                    //_effect->Shader->SetDiffuse(cmdList, command.Texture);
                    _effect->Shader->SetDiffuse(cmdList, group.front().Texture);

                    for (auto& command : group) {
                        _batch.DrawQuad(command.V0, command.V1, command.V2, command.V3);
                    }
                    _batch.End();
                }

                layer.clear();
            }
        }

        // Draws text using Descent fonts at 1:1 scaling of the original pixels.
        void DrawGameTextUnscaled(string_view str, DrawTextInfo info) {
            info.Scale /= _scale;
            //info.Position /= _scale;
            DrawGameText(str, info);
        }

        // Returns foreground and background color of text
        static Tuple<Color, Color> GetTextColor(char next) {
            // todo: change based on D1 / D2. Add extended colors.
            if (next == '1') {
                return { ColorFromRGB(0, 219, 0), ColorFromRGB(0, 75, 0) };
            }
            else if (next == '2') {
                return { ColorFromRGB(163, 151, 147), ColorFromRGB(19, 19, 27) };
            }
            else if (next == '3') {
                return { ColorFromRGB(100, 109, 117), ColorFromRGB(19, 19, 27) };
            }
            else {
                return { Color(0, 1, 0), Color(0, 0.25f, 0) };
            }
        }

        // Draws text using Descent fonts, scaled to be a constant size based on the output height.
        void DrawGameText(string_view str, const DrawTextInfo& info, int layer = 1) {
            float xOffset = 0, yOffset = 0;
            auto font = Atlas.GetFont(info.Font);
            if (!font) return;

            auto color = info.Color;
            auto background = color * 0.1f;
            background.w = 1.0f;

            auto scale = info.Scale * _scale * font->Scale;
            auto strSize = MeasureString(str, info.Font) * scale;
            Vector2 alignment = GetAlignment(strSize, info.HorizontalAlign, info.VerticalAlign, _size);
            bool inToken = false;

            for (int i = 0; i < str.size(); i++) {
                auto c = str[i];
                if (c == '\n') {
                    xOffset = 0;
                    yOffset += (font->Height + FONT_LINE_SPACING) * scale;
                    continue;
                }

                char next = i + 1 >= str.size() ? 0 : str[i + 1];

                if (c == '$') {
                    inToken = true;
                    continue;
                }

                if (c == '\t') {
                    xOffset = info.TabStop * scale;
                    continue;
                }

                if (inToken) {
                    if (c == 'C') {
                        std::tie(color, background) = GetTextColor(next);
                    }

                    i++;
                    inToken = false;
                    continue;
                }

                auto& ci = Atlas.GetCharacter(c, info.Font);
                auto x0 = alignment.x + xOffset + info.Position.x * scale;
                auto y0 = alignment.y + yOffset + info.Position.y * scale;

                Vector2 charSize = Vector2(font->GetWidth(c), font->Height) * scale;
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

                DrawBitmap(cbi, layer + 1); // Foreground

                auto kerning = Atlas.GetKerning(c, next, info.Font) * scale;
                xOffset += charSize.x + kerning;
            }
        }

        void DrawCharacter(char c, const Vector2& position, FontSize fontSize, const Color& color, float scale = 1, int layer = 1) {
            auto font = Atlas.GetFont(fontSize);
            auto& ci = Atlas.GetCharacter(c, fontSize);
            CanvasBitmapInfo cbi;
            cbi.Position = position;
            cbi.Size = Vector2(font->GetWidth(c), font->Height) * scale;
            cbi.UV0 = Vector2{ ci.X0, ci.Y0 };
            cbi.UV1 = Vector2{ ci.X1, ci.Y1 };
            cbi.Color = color;
            cbi.Texture = Render::StaticTextures->Font.GetSRV();
            //cbi.Position.x += charSize.x;
            DrawBitmap(cbi, layer);
        }

        // Draw text that fades in based on the elapsed time.
        // Returns true when all text is drawn
        bool DrawFadingText(string_view str, const DrawTextInfo& info, float elapsed, float speed, bool showCursor = false, int layer = 1) {
            float xOffset = 0, yOffset = 0;
            auto font = Atlas.GetFont(info.Font);
            if (!font) return true;

            auto color = info.Color;
            Color background = color * 0.1f;
            background.w = 1.0f;

            auto scale = info.Scale * _scale * font->Scale;
            auto strSize = MeasureString(str, info.Font) * scale;
            Vector2 alignment = GetAlignment(strSize, info.HorizontalAlign, info.VerticalAlign, _size);
            bool inToken = false;

            int maxChars = int(elapsed / speed);
            int displayedChars = 0;
            Vector2 cursor;
            bool finished = false;

            for (int i = 0; i < str.size(); i++) {
                if (i + 1 >= str.size()) finished = true;

                auto c = str[i];
                if (c == '\n') {
                    xOffset = 0;
                    yOffset += (font->Height + FONT_LINE_SPACING) * scale;
                    continue;
                }

                char next = i + 1 >= str.size() ? 0 : str[i + 1];

                if (c == '$') {
                    inToken = true;
                    continue;
                }

                if (c == '\t') {
                    xOffset = info.TabStop;
                    continue;
                }

                if (inToken) {
                    if (c == 'C')
                        std::tie(color, background) = GetTextColor(next);

                    i++;
                    inToken = false;
                    continue;
                }

                // if character is recently displayed, highlight it
                auto t = 1 - Saturate((elapsed - displayedChars * speed) * 1.5f);
                auto colorMult = t > 0 ? std::powf(t, 2) : 0;
                auto glow = Color(colorMult, colorMult, colorMult, 0);

                //auto& ci = Atlas.GetCharacter(c, info.Font);
                cursor.x = alignment.x + xOffset + info.Position.x;
                cursor.y = alignment.y + yOffset + info.Position.y;
                auto charSize = Vector2(font->GetWidth(c), font->Height) * scale;

                if (displayedChars++ > maxChars) {
                    if (showCursor)
                        DrawCharacter('_', cursor, info.Font, color + glow, 1, layer);

                    break;
                }

                DrawCharacter(c, cursor + Vector2{ -1 * scale, 1 * scale }, info.Font, background, 1, layer); // shadow
                DrawCharacter(c, cursor, info.Font, color + glow, 1, layer + 1);

                auto kerning = Atlas.GetKerning(c, next, info.Font) * scale;
                xOffset += charSize.x + kerning;
            }

            if (finished && showCursor) {
                double whole{};

                if (std::modf(Clock.GetTotalTimeSeconds(), &whole) > 0.5f)
                    DrawCharacter('_', Vector2(info.Position.x, cursor.y + font->Height + FONT_LINE_SPACING), info.Font, color, 1, layer + 1);
            }

            return finished;
        }
    };

    struct HudCanvasPayload {
        HudVertex V0, V1, V2, V3;
        D3D12_GPU_DESCRIPTOR_HANDLE Texture{};
        int Layer = 0;
        float Scanline = 0;
    };

    class HudCanvas2D {
        // Layers -> Texture id -> commands
        Array<Dictionary<uint64, List<HudCanvasPayload>>, 10> _commands;
        DirectX::PrimitiveBatch<HudVertex> _batch;
        Effect<HudShader>* _effect;
        Vector2 _size = { 1024, 1024 };
        float _scale = 1;

    public:
        HudCanvas2D(ID3D12Device* device, Effect<HudShader>& effect) : _batch(device), _effect(&effect) {}

        // Sets the size of the canvas. Affects alignment.
        void SetSize(uint width, uint height, uint targetScreenHeight = 480) {
            _size = Vector2{ (float)width, (float)height };
            _scale = (float)height / targetScreenHeight; // scaling due to original screen size being 480 pixels
        }

        float GetScale() const { return _scale; }
        const Vector2& GetSize() const { return _size; }

        void DrawBitmap(const CanvasBitmapInfo& info, int layer = 0);
        void DrawBitmapScaled(const CanvasBitmapInfo& info, int layer = 0);
        void Render(GraphicsContext& ctx);
        void DrawGameText(string_view str, const DrawTextInfo& info, int layer = 1);

        void Draw(const HudCanvasPayload& payload) {
            if (!payload.Texture.ptr) return;
            auto layer = std::clamp(payload.Layer, 0, (int)_commands.size() - 1);
            _commands[layer][payload.Texture.ptr].push_back(payload);
        }
    };
}
