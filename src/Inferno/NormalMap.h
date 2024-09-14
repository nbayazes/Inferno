#pragma once
#include "Pig.h"
#include "Types.h"

namespace Inferno {
    inline float GetIntensity(const Palette::Color& color, bool invert = false) {
        // assumes color is clamped 0..1
        //Vector3 rgb = { (float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f };
        //auto height = Luminance(rgb);
        auto c = color.ToColor();
        //c.AdjustSaturation(0);
        //auto v = c.x;
        auto hsv = Vector3(DirectX::XMColorRGBToHSV(c));
        auto v = hsv.z;
        return invert ? 1.0f - v : v;
    }

    inline List<uint8> CreateSpecularMap(const PigBitmap& image, float brightness = 0.5f, float contrast = 1.0f, bool invert = false) {
        List<uint8> specularMap(image.Data.size());
        if (image.Data.empty()) return specularMap;

        //uint8 min = 255, max = 0;

        for (int y = 0; y < image.Info.Height; y++) {
            for (int x = 0; x < image.Info.Width; x++) {
                auto color = image.Data[y * image.Info.Width + x].ToColor();
                if (invert) color.Negate();
                color.AdjustSaturation(0);
                color.AdjustContrast(contrast);
                specularMap[y * image.Info.Width + x] = uint8(color.x * brightness * 255);
                //min = std::min(min, specularMap[y * image.Info.Width + x]);
                //max = std::max(max, specularMap[y * image.Info.Width + x]);
            }
        }

        //float ratio = max == 0 ? 0 : (float)(max - min) / float(max);
        //uint8 delta = max - min;
        // delta = 100
        // min = 50, max = 150
        // -50 everything -> 0, 100. * (255 / delta) -> 255
        /*if (delta > 0) {
            float ratio = 255.0f / delta;
            for (auto& p : specularMap) {
                p = uint8((float)(p - min) * ratio);
            }
        }*/

        return specularMap;
    }

    struct NormalMapOptions {
        float Strength = 1.0f;
        bool Invert = true;
        bool Tileable = true;
    };

    // Creates a normal map using a Sobel kernel
    inline List<Palette::Color> CreateNormalMap(const PigBitmap& image, const NormalMapOptions& options = {}) {
        List<Palette::Color> normalMap(image.Data.size());
        if (image.Data.empty()) return normalMap;

        const auto width = image.Info.Width;
        const auto height = image.Info.Height;
        const float strengthInv = 1 / options.Strength;

        auto pixelAt = [&](int x, int y) {
            if (x >= image.Info.Width) {
                x = options.Tileable ? 0 : image.Info.Width - 1;
            }
            else if (x < 0) {
                x = options.Tileable ? image.Info.Width - 1 : 0;
            }

            if (y >= image.Info.Height) {
                y = options.Tileable ? 0 : image.Info.Height - 1;
            }
            else if (y < 0) {
                y = options.Tileable ? image.Info.Height - 1 : 0;
            }

            return image.Data[y * width + x];
        };

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                const auto topLeft = pixelAt(x - 1, y - 1);
                const auto top = pixelAt(x, y - 1);
                const auto topRight = pixelAt(x + 1, y - 1);

                const auto left = pixelAt(x - 1, y);
                const auto right = pixelAt(x + 1, y);

                const auto bottomLeft = pixelAt(x - 1, y + 1);
                const auto bottom = pixelAt(x, y + 1);
                const auto bottomRight = pixelAt(x + 1, y + 1);

                const auto tl = GetIntensity(topLeft, options.Invert);
                const auto t = GetIntensity(top, options.Invert);
                const auto tr = GetIntensity(topRight, options.Invert);
                const auto r = GetIntensity(right, options.Invert);
                const auto br = GetIntensity(bottomRight, options.Invert);
                const auto b = GetIntensity(bottom, options.Invert);
                const auto bl = GetIntensity(bottomLeft, options.Invert);
                const auto l = GetIntensity(left, options.Invert);

                constexpr float weight = 1.0f; // 2 for sobel, 1 for prewitt. sobel looks too smooth.
                const auto dX = (tr + weight * r + br) - (tl + weight * l + bl);
                const auto dY = (bl + weight * b + br) - (tl + weight * t + tr);
                const auto dZ = strengthInv;

                // Cheaper approach that is sharper but more grainy
                //dX = r - l
                //dY = b - t
                //dZ = .5f;

                Vector3 c(dX, dY, dZ);
                c.Normalize();
                auto mapPixel = [](float p) { return uint8((p + 1.0f) * (255.0f / 2.0f)); };
                normalMap[y * width + x] = { mapPixel(c.x), mapPixel(c.y), mapPixel(c.z) };
            }
        }

        return normalMap;
    }
}
