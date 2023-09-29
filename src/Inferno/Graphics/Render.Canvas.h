#pragma once

#include "Types.h"
#include "DirectX.h"
#include "ShaderLibrary.h"
#include "CommandContext.h"
#include "Game.Text.h"

namespace Inferno::Render {
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

    // Draws a quad to the 2D canvas (UI Layer)
    class Canvas2D {
        DirectX::PrimitiveBatch<CanvasVertex> _batch;
        Dictionary<uint64, List<CanvasPayload>> _commands;
        Effect<UIShader>* _effect;
        Vector2 _size = { 1024, 1024 };
        float _scale = 1;

    public:
        Canvas2D(ID3D12Device* device, Effect<UIShader>& effect) : _batch(device), _effect(&effect) {}

        // Sets the size of the canvas. Affects alignment.
        void SetSize(uint width, uint height, uint targetScreenHeight = 480) {
            _size = Vector2{ (float)width, (float)height };
            _scale = (float)height / targetScreenHeight; // scaling due to original screen size being 480 pixels
        }

        float GetScale() const { return _scale; }

        void Draw(const CanvasPayload& payload) {
            if (!payload.Texture.ptr) return;
            _commands[payload.Texture.ptr].push_back(payload);
        }

        void DrawBitmap(TexID id, const Vector2& pos, const Vector2& size, const Color& color = { 1, 1, 1 });

        void DrawBitmap(D3D12_GPU_DESCRIPTOR_HANDLE texture, const Vector2& pos, const Vector2& size, const Color& color = { 1, 1, 1 });

        void DrawBitmap(D3D12_GPU_DESCRIPTOR_HANDLE texture, const Vector2& pos, const Vector2& size, const Vector2& uv0, const Vector2& uv1, const Color& color = { 1, 1, 1 });

        void DrawBitmap(const CanvasBitmapInfo& info);

        void Render(Graphics::GraphicsContext& ctx);

        // Draws text using Descent fonts at 1:1 scaling of the original pixels.
        void DrawGameTextUnscaled(string_view str,
                                  float x, float y,
                                  FontSize size,
                                  Color color = Color(1, 1, 1),
                                  float scale = 1,
                                  AlignH alignH = AlignH::Left, AlignV alignV = AlignV::Top) {
            DrawGameText(str, x, y, size, color, scale / _scale, alignH, alignV);
        }

        // Draws text using Descent fonts, scaled to be a constant size based on the output height.
        void DrawGameText(string_view str,
                          float x, float y,
                          FontSize size,
                          Color color = Color(1, 1, 1),
                          float scale = 1,
                          AlignH alignH = AlignH::Left, AlignV alignV = AlignV::Top);
    };

    struct HudCanvasPayload {
        HudVertex V0, V1, V2, V3;
        D3D12_GPU_DESCRIPTOR_HANDLE Texture{};
        float Scanline = 0;
    };

    class HudCanvas2D {
        DirectX::PrimitiveBatch<HudVertex> _batch;
        Dictionary<uint64, List<HudCanvasPayload>> _commands;
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

        void DrawBitmap(const CanvasBitmapInfo& info);

        void Render(Graphics::GraphicsContext& ctx);

        void DrawGameText(string_view str, const DrawTextInfo& info);

        void Draw(const HudCanvasPayload& payload) {
            if (!payload.Texture.ptr) return;
            _commands[payload.Texture.ptr].push_back(payload);
        }
    };

    Vector2 GetAlignment(const Vector2& size, AlignH alignH, AlignV alignV, const Vector2& screenSize);
}
