#pragma once

#include "Graphics/Render.h"

namespace Inferno {
    constexpr int BASE_RESOLUTION_Y = 480; // 'high-res' assets base resolution

    void DrawHUDBitmap(const Vector2& size, const Vector2& offset, int gauge) {
        TexID id = Resources::GameData.HiResGauges[gauge];
        auto& ti = Resources::GetTextureInfo(id);
        auto pos = size / 2;
        auto scale = size.y / BASE_RESOLUTION_Y;

        Vector2 scaledSize = { ti.Width * scale, ti.Height * scale };
        pos.x -= scaledSize.x / 2;
        pos += offset * scale;
        Render::Canvas->DrawBitmap(id, pos, scaledSize);
    }

    void DrawReticle(const Vector2& size) {
        constexpr auto RETICLE_CROSS = 46;
        constexpr auto RETICLE_PRIMARY = 48; // not ready, +1 center ready, +2 quad ready
        constexpr auto RETICLE_SECONDARY = 51; // not ready, left ready, right ready, center not ready (wrong alignment), center ready
        //constexpr auto RETICLE_LAST = 55;

        const Vector2 crossOffset(0/*-8*/, -5);
        const Vector2 primaryOffset(0/*-30*/, 14);
        const Vector2 secondaryOffset(0/*-24*/, 2);

        DrawHUDBitmap(size, crossOffset, RETICLE_CROSS + 1); // gauss, vulkan
        DrawHUDBitmap(size, primaryOffset, RETICLE_PRIMARY + 2);
        DrawHUDBitmap(size, secondaryOffset, RETICLE_SECONDARY + 4);

        //TexID id = Resources::GameData.HiResGauges[RETICLE_PRIMARY];
        //auto& ti = Resources::GetTextureInfo(id);
        //auto pos = size / 2;
        //auto ratio = size.y / BASE_RESOLUTION_Y;

        //Vector2 scaledSize = { ti.Width * ratio, ti.Height * ratio };
        //pos -= scaledSize / 2;
        //Render::Canvas->DrawBitmap(id, pos, scaledSize);
    }

    void DrawHUD(const Vector2& size) {
        DrawReticle(size);
    }

}