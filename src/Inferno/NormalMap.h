#pragma once
#include "Pig.h"
#include "Types.h"

namespace Inferno {
    struct NormalMapOptions {
        float Strength = 1.0f;
        bool Invert = false;
        bool Tileable = true;
    };

    inline float Luminance(const Vector3& v) {
        return Vector3(0.2126f, 0.7152f, 0.0722f).Dot(v);
    }

    inline float GetHeight(const Palette::Color& color, bool invert) {
        // assumes color is clamped 0..1
        //Vector3 rgb = { (float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f };
        //auto height = Luminance(rgb);
        auto height = ((float)color.r + (float)color.g + (float)color.b) / 3.0f / 255.0f;
        return invert ? 1.0f - height : height;
    }

    inline Vector3 Sobel(const float kernel[3][3], float strengthInv) {
        const auto top = kernel[0][0] + 2 * kernel[0][1] + kernel[0][2];
        const auto bottom = kernel[2][0] + 2 * kernel[2][1] + kernel[2][2];
        const auto right = kernel[0][2] + 2 * kernel[1][2] + kernel[2][2];
        const auto left = kernel[0][0] + 2 * kernel[1][0] + kernel[2][0];

        const auto dY = right - left;
        const auto dX = bottom - top;
        const auto dZ = strengthInv;

        Vector3 v(dX, dY, dZ);
        v.Normalize();
        return v;
    }

    inline Vector3 Prewitt(const float kernel[3][3], float strengthInv) {
        const auto top = kernel[0][0] + kernel[0][1] + kernel[0][2];
        const auto bottom = kernel[2][0] + kernel[2][1] + kernel[2][2];
        const auto right = kernel[0][2] + kernel[1][2] + kernel[2][2];
        const auto left = kernel[0][0] + kernel[1][0] + kernel[2][0];

        const auto dY = right - left;
        const auto dX = top - bottom;
        const auto dZ = strengthInv;

        Vector3 v(dX, dY, dZ);
        v.Normalize();
        return v;
    }

    inline List<Palette::Color> CreateNormalMap(const PigBitmap& image, const NormalMapOptions& options) {
        List<Palette::Color> normalMap(image.Data.size());
        List<float> heightMap(image.Data.size());

        for (int i = 0; i < image.Data.size(); ++i) {
            heightMap[i] = GetHeight(image.Data[i], options.Invert);
            //auto height = Color(heightMap[i], heightMap[i], heightMap[i]);
            //normalMap[i] = Palette::Color::FromColor(height);
            //normalMap[i] = image.Data[i].r, image.Data[i].g, image.Data[i].b;
        }
        //return normalMap;

        float strengthInv = 1 / options.Strength;

        const auto width = image.Width;
        const auto height = image.Height;

        auto heightAt = [&](int x, int y) {
            return heightMap[y * width + x];
        };

        // wraps edges based on the tileable setting
        auto wrap = [&](int index, int maxValue) {
            if (index >= maxValue) {
                return options.Tileable ? maxValue - index : maxValue - 1;
            }
            else if (index < 0) {
                return options.Tileable ? maxValue + index : 0;
            }
            else {
                return index;
            }
        };

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                // surrounding pixels
                const auto topLeft = heightAt(wrap(x - 1, width), wrap(y - 1, height));
                const auto top = heightAt(wrap(x - 1, width), wrap(y, height));
                const auto topRight = heightAt(wrap(x - 1, width), wrap(y + 1, height));

                const auto right = heightAt(wrap(x, width), wrap(y + 1, height));
                const auto left = heightAt(wrap(x, width), wrap(y - 1, height));

                const auto bottomRight = heightAt(wrap(x + 1, width), wrap(y + 1, height));
                const auto bottom = heightAt(wrap(x + 1, width), wrap(y, height));
                const auto bottomLeft = heightAt(wrap(x + 1, width), wrap(y - 1, height));

                // Convolution kernel
                const float kernel[3][3] = {
                    { topLeft, top, topRight },
                    { left, 0, right },
                    { bottomLeft, bottom, bottomRight }
                };

                auto normal = Prewitt(kernel, strengthInv);
                //auto normal = Sobel(kernel, strengthInv);
                normalMap[y * width + x] = Palette::Color::FromColor(Color(normal));
            }
        }

        return normalMap;
    }

    inline List<Palette::Color> CreateNormalMap2(const PigBitmap& image, const NormalMapOptions& options) {
        List<Palette::Color> normalMap(image.Data.size());

        const auto width = image.Width;
        const auto height = image.Height;
        const float strengthInv = 1 / options.Strength;

        auto pixelAt = [&](int x, int y) {
            if (x >= image.Width) {
                x = options.Tileable ? 0 : image.Width - 1;
            }
            else if (x < 0) {
                x = options.Tileable ? image.Width - 1 : 0;
            }

            if (y >= image.Height) {
                y = options.Tileable ? 0 : image.Height - 1;
            }
            else if (y < 0) {
                y = options.Tileable ? image.Height - 1 : 0;
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

                const auto tl = GetHeight(topLeft, options.Invert);
                const auto t = GetHeight(top, options.Invert);
                const auto tr = GetHeight(topRight, options.Invert);
                const auto r = GetHeight(right, options.Invert);
                const auto br = GetHeight(bottomRight, options.Invert);
                const auto b = GetHeight(bottom, options.Invert);
                const auto bl = GetHeight(bottomLeft, options.Invert);
                const auto l = GetHeight(left, options.Invert);

                constexpr float weight = 1.0f; // 2 for sobel, 1 for prewitt 
                const auto dX = (tr + weight * r + br) - (tl + weight * l + bl);
                const auto dY = (bl + weight * b + br) - (tl + weight * t + tr);
                const auto dZ = strengthInv;

                Vector3 c(dX, dY, dZ);
                c.Normalize();
                auto mapPixel = [](float p) { return uint8((p + 1.0f) * (255.0f / 2.0f)); };
                normalMap[y * width + x] = { mapPixel(c.x), mapPixel(c.y), mapPixel(c.z) };
            }
        }

        return normalMap;
    }
}
