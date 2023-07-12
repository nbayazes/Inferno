#include "pch.h"
#include "Procedural.h"

// Descent 3 procedural water effects
//
// Most of this code is credited to the efforts of SaladBadger

namespace Inferno {
    List<ubyte> WaterProcTableLo;
    List<ushort> WaterProcTableHi;

    void InitWaterTables() {
        WaterProcTableLo.resize(16384);
        WaterProcTableHi.resize(16384);

        for (int i = 0; i < 64; i++) {
            float intensity1 = i * 0.01587302f;
            float intensity2 = intensity1 * 2;
            if (intensity2 > 1.0)
                intensity2 = 1.0;

            intensity1 = (intensity1 - .5f) * 2;
            if (intensity1 < 0)
                intensity1 = 0;

            for (int j = 0; j < 32; j++) {
                auto channel = ushort(j * intensity2 + intensity1 * 31.0f);
                if (channel > 31)
                    channel = 31;

                for (int k = 0; k < 4; k++)
                    WaterProcTableHi[((i * 64) + j) * 4 + k] = (channel | 65504u) << 10u;
            }

            for (int j = 0; j < 32; j++) {
                auto channel = ubyte(j * intensity2 + intensity1 * 31.0f);
                if (channel > 31)
                    channel = 31;

                for (int k = 0; k < 8; k++)
                    WaterProcTableLo[(i * 256) + j + (32 * k)] = channel;
            }

            for (int j = 0; j < 8; j++) {
                auto channel = ushort(j * intensity2 + intensity1 * 7.0f);
                if (channel > 7)
                    channel = 7;

                for (int k = 0; k < 32; k++)
                    WaterProcTableLo[(i * 256) + (j * 32) + k] |= channel << 5;
            }

            for (int j = 0; j < 4; j++) {
                auto channel = ushort((j * 8) * intensity2 + intensity1 * 24.0f);
                if (channel > 24)
                    channel = 24;

                for (int k = 0; k < 32; k++)
                    WaterProcTableHi[(i * 256) + j + (k * 4)] |= channel << 5;
            }
        }
    }

    constexpr int16 RGB32ToBGR16(int r, int g, int b) {
        return int16((b >> 3) + ((g >> 3) << 5) + ((r >> 3) << 10));
    }


    Palette::Color AverageColor(const Palette::Color& a, const Palette::Color& b) {
        return {
            uint8((a.r + b.r) >> 1),
            uint8((a.g + b.g) >> 1),
            uint8((a.b + b.b) >> 1),
            uint8((a.a + b.a) >> 1)

        };
    }

    Palette::Color AverageColor(const Palette::Color& a, const Palette::Color& b, float weight) {
        return {
            uint8(a.r * (1 - weight) + b.r * weight),
            uint8(a.g * (1 - weight) + b.g * weight),
            uint8(a.b * (1 - weight) + b.b * weight),
            uint8(a.a * (1 - weight) + b.a * weight)
        };
    }

    Palette::Color BilinearSample(int x, int y, int srcResmaskX, int srcResmaskY, const PigBitmap& src) {
        // x and y are 128x128 ...
        auto texWidth = src.Info.Width;
        int offset00 = (y & srcResmaskY) * texWidth + (x & srcResmaskX);
        int offset01 = (y & srcResmaskY) * texWidth + (x + 1 & srcResmaskX);
        int offset10 = (y + 1 & srcResmaskY) * texWidth + (x & srcResmaskX);
        int offset11 = (y + 1 & srcResmaskY) * texWidth + (x + 1 & srcResmaskX);

        auto& c00 = src.Data[offset00];
        auto& c01 = src.Data[offset01];
        auto& c10 = src.Data[offset10];
        auto& c11 = src.Data[offset11];

        auto x0 = AverageColor(c00, c01);
        auto x1 = AverageColor(c10, c11);
        return AverageColor(x0, x1);
    }

    List<Palette::Color> BilinearUpscale(const PigBitmap& src, int outputWidth) {
        assert(src.Info.Width == 64 && src.Info.Height == 64);

        auto srcWidth = src.Info.Width;
        auto srcHeight = src.Info.Height;

        List<Palette::Color> output;
        output.resize(outputWidth * outputWidth);

        auto ratioX = float(srcWidth) / outputWidth;
        auto ratioY = float(srcHeight) / outputWidth;

        // output size is 128
        for (int y = 0; y < outputWidth; y++) {
            for (int x = 0; x < outputWidth; x++) {
                // offset uv by 0.5 to smooth the result
                float u = x + 0.5f;
                float v = y + 0.5f;

                int xl = (int)floor(ratioX * u) % srcWidth;
                int yl = (int)floor(ratioY * v) % srcHeight;
                int xh = (int)ceil(ratioX * u) % srcWidth;
                int yh = (int)ceil(ratioY * v) % srcHeight;

                float xWeight = (ratioX * u) - xl;
                float yWeight = (ratioY * v) - yl;

                auto& c00 = src.Data[yl * srcWidth + xl];
                auto& c10 = src.Data[yl * srcWidth + xh];
                auto& c01 = src.Data[yh * srcWidth + xl];
                auto& c11 = src.Data[yh * srcWidth + xh];

                auto bot = AverageColor(c00, c10, xWeight);
                auto top = AverageColor(c01, c11, xWeight);
                output[y * outputWidth + x] = AverageColor(bot, top, yWeight);
            }
        }

        return output;
    }

    class ProceduralWater : public ProceduralTextureBase {
        PigBitmap _baseTexture;
        List<int16> _waterBuffer[2]{};

    public:
        ProceduralWater(const Outrage::TextureInfo& info, TexID baseTexture)
            : ProceduralTextureBase(info, baseTexture) {
            _waterBuffer[0].resize(TotalSize);
            _waterBuffer[1].resize(TotalSize);
            auto& texture = Resources::GetBitmap(baseTexture);
            _baseTexture.Data = BilinearUpscale(texture, Resolution);
            _baseTexture.Info.Width = Resolution;
            _baseTexture.Info.Height = Resolution;
        }

    protected:
        void OnUpdate() override {
            for (auto& elem : Info.Procedural.Elements) {
                switch (elem.WaterType) {
                    case Outrage::WaterProceduralType::HeightBlob:
                        AddWaterHeightBlob(elem);
                        break;
                    case Outrage::WaterProceduralType::SineBlob:
                        AddWaterSineBlob(elem);
                        break;
                    case Outrage::WaterProceduralType::RandomRaindrops:
                        AddWaterRaindrops(elem);
                        break;
                    case Outrage::WaterProceduralType::RandomBlobdrops:
                        AddWaterBlobdrops(elem);
                        break;
                }
            }

            //if (EasterEgg) {
            //    if (Easter_egg_handle != -1) {
            //        ushort* srcData = bm_data(Easter_egg_handle, 0);
            //        short* heightData = (short*)proc->proc1;
            //        int width = bm_w(Easter_egg_handle, 0);
            //        int height = bm_w(Easter_egg_handle, 0); //oops, they get bm_w twice..

            //        if (width <= RESOLUTION && height <= RESOLUTION && width > 0 && height > 0) {
            //            int offset = (RESOLUTION / 2 - height / 2) * RESOLUTION + (RESOLUTION / 2 - width / 2);
            //            for (int y = 0; y < height; y++) {
            //                for (int x = 0; x < width; x++) {
            //                    if (*srcData & 128)
            //                        heightData[offset + x] += 200;

            //                    srcData++;
            //                }
            //                offset += RESOLUTION;
            //            }
            //        }
            //    }
            //    EasterEgg = false;
            //}

            UpdateWater();

            if (Info.Procedural.Light > 0)
                DrawWaterWithLight(Info.Procedural.Light - 1);
            else
                DrawWaterNoLight();
        }


        struct BlobBounds {
            int minX, minY;
            int maxX, maxY;
            int sizeSq;
        };

        BlobBounds GetBlobBounds(const Element& elem) const {
            const auto sizeSq = elem.Size * elem.Size;
            int minX = -elem.Size;
            int minY = -elem.Size;
            if (elem.X1 + minX < 1)
                minX = 1 - elem.X1;
            if (elem.Y1 + minY < 1)
                minY = 1 - elem.Y1;

            auto maxX = (int)elem.Size;
            auto maxY = (int)elem.Size;

            if (elem.X1 + maxX > Resolution - 1)
                maxX = Resolution - elem.X1 - 1;
            if (elem.Y1 + maxY > Resolution - 1)
                maxY = Resolution - elem.Y1 - 1;

            return { minX, minY, maxX, maxY, sizeSq };
        }

        void AddWaterHeightBlob(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto blob = GetBlobBounds(elem);

            for (int y = blob.minY; y < blob.maxY; y++) {
                auto yOffset = (y + elem.Y1) * Resolution;
                for (int x = blob.minX; x < blob.maxX; x++) {
                    auto offset = yOffset + x + elem.X1;
                    if (x * x + y * y < blob.sizeSq)
                        _waterBuffer[_index][offset] += elem.Speed;
                }
            }
        }

        void AddWaterSineBlob(const Element& elem) {
            if (!ShouldDrawElement(elem))
                return;

            auto blob = GetBlobBounds(elem);

            for (int y = blob.minY; y < blob.maxY; y++) {
                auto yOffset = (y + elem.Y1) * Resolution;
                for (int x = blob.minX; x < blob.maxX; x++) {
                    auto offset = yOffset + x + elem.X1;
                    auto radSq = x * x + y * y;
                    if (radSq < blob.sizeSq) {
                        int fix = (int)sqrt(radSq * (1024.0f / elem.Size) * (1024.0f / elem.Size));
                        float cosine = cos(fix / 65536.0f * DirectX::XM_2PI);
                        int add = int(cosine * elem.Speed);
                        // (add + (add < 0 ? 1 : 0)) / 8
                        _waterBuffer[_index][offset] += int16((add + (add >> 31 & 7)) >> 3);
                    }
                }
            }
        }

        void AddWaterRaindrops(const Element& elem) {
            if (!ShouldDrawElement(elem)) return;

            Element drop = {
                .WaterType = Outrage::WaterProceduralType::HeightBlob,
                .Speed = (int8)std::max(0, elem.Speed * Rand(-5, 5)),
                .Frequency = 0,
                .Size = (uint8)Rand(1, 4),
                .X1 = uint8(Rand(-elem.Size, elem.Size) + elem.X1),
                .Y1 = uint8(Rand(-elem.Size, elem.Size) + elem.Y1),
            };

            AddWaterHeightBlob(drop);
        }

        void AddWaterBlobdrops(const Element& elem) {
            if (!ShouldDrawElement(elem)) return;

            Element drop = {
                .WaterType = Outrage::WaterProceduralType::HeightBlob,
                .Speed = (int8)std::max(0, elem.Speed * Rand(-25, 25)),
                .Frequency = 0,
                .Size = (uint8)Rand(4, 10),
                .X1 = uint8(Rand(-elem.Size, elem.Size) + elem.X1),
                .Y1 = uint8(Rand(-elem.Size, elem.Size) + elem.Y1),
            };

            AddWaterHeightBlob(drop);
        }

        void UpdateWater() {
            int factor = Info.Procedural.Thickness;
            if (Info.Procedural.OscillateTime > 0) {
                int thickness = Info.Procedural.Thickness;
                int oscValue = Info.Procedural.OscillateValue;
                if (thickness < oscValue) {
                    oscValue = thickness;
                    thickness = oscValue;
                }

                int delta = thickness - oscValue;
                if (delta > 0) {
                    int time = (int)(Render::ElapsedTime / Info.Procedural.OscillateTime / delta) % (delta * 2);
                    if (time < delta)
                        time %= delta;
                    else
                        time = (delta - time % delta) - 1;

                    factor = time + oscValue;
                }
            }

            auto& src = _waterBuffer[_index];
            auto& dest = _waterBuffer[1 - _index];

            factor &= 31;

            // Handle dampening within the center of the heightmaps
            for (int y = 1; y < Resolution - 1; y++) {
                for (int x = 1; x < Resolution - 1; x++) {
                    auto offset = y * Resolution + x;
                    auto sum = ((src[offset + Resolution] + src[offset - 1] + src[offset + 1] + src[offset - Resolution]) >> 1) - dest[offset];
                    dest[offset] = int16(sum - (sum >> factor));
                }
            }

            // Handle dampening on the edges of the heightmaps
            for (int y = 0; y < Resolution; y++) {
                int belowoffset, aboveoffset;

                if (y == 0) {
                    aboveoffset = -(Resolution - 1) * Resolution;
                    belowoffset = Resolution;
                }
                else if (y == Resolution - 1) {
                    aboveoffset = Resolution;
                    belowoffset = -(Resolution - 1) * Resolution;;
                }
                else {
                    belowoffset = aboveoffset = Resolution;
                }

                for (int x = 0; x < Resolution; x++) {
                    //only dampen if actually on an edge
                    if (y == 0 || y == Resolution - 1 || x == 0 || x == Resolution - 1) {
                        int leftoffset, rightoffset;
                        if (x == 0) {
                            leftoffset = -(Resolution - 1);
                            rightoffset = 1;
                        }
                        else if (x == Resolution - 1) {
                            leftoffset = 1;
                            rightoffset = -(Resolution - 1);
                        }
                        else {
                            leftoffset = rightoffset = 1;
                        }

                        int offset = y * Resolution + x;
                        int sum = ((src[offset - leftoffset] + src[offset + rightoffset] + src[offset - aboveoffset] + src[offset + belowoffset]) >> 1) - dest[offset];

                        dest[offset] = int16(sum - (sum >> factor));
                    }
                }
            }
        }


        void DrawWaterWithLight(int lightFactor) {
            auto& heights = _waterBuffer[_index];
            int lightshift = lightFactor & 31;

            auto& texture = _baseTexture;
            auto xScale = (float)texture.Info.Width / Resolution;
            auto yScale = (float)texture.Info.Height / Resolution;
            auto srcResmaskX = texture.Info.Width - 1;
            auto srcResmaskY = texture.Info.Height - 1;

            for (int y = 0; y < Resolution; y++) {
                int topoffset, botoffset;
                if (y == (Resolution - 1)) {
                    botoffset = _resMask * Resolution;
                    topoffset = Resolution;
                }
                else if (y == 0) {
                    botoffset = -Resolution;
                    topoffset = -_resMask * Resolution;
                }
                else {
                    topoffset = Resolution;
                    botoffset = -Resolution;
                }

                for (int x = 0; x < Resolution; x++) {
                    int offset = y * Resolution + x;

                    int horizheight;
                    if (x == Resolution - 1)
                        horizheight = heights[offset - 1] - heights[offset - Resolution + 1];
                    else if (x == 0)
                        horizheight = heights[offset + Resolution - 1] - heights[offset + 1];
                    else
                        horizheight = heights[offset - 1] - heights[offset + 1];

                    int vertheight = heights[offset - topoffset] - heights[offset - botoffset];

                    int lightval = 32 - (horizheight >> lightshift);
                    if (lightval > 63)
                        lightval = 63;
                    if (lightval < 0)
                        lightval = 0;

                    int xShift = int((horizheight >> 3) + x * xScale) % texture.Info.Width;
                    int yShift = int((vertheight >> 3) + y * yScale) % texture.Info.Width;

                    int destOffset = y * Resolution + x;

                    int srcOffset = (yShift & srcResmaskY) * texture.Info.Width + (xShift & srcResmaskX);
                    auto& c = texture.Data[srcOffset]; // RGBA8888
                    //auto c = BilinearSample(xShift, yShift, srcResmaskX, srcResmaskY, texture);
                    auto srcPixel = RGB32ToBGR16(c.r, c.g, c.b);

                    auto rgba8881 =
                        WaterProcTableLo[(srcPixel & 255) + lightval * 256] +
                        WaterProcTableHi[((srcPixel >> 8) & 127) + lightval * 256];

                    _pixels[destOffset] = BGRA16ToRGB32(rgba8881, c.a);
                }
            }
        }


        void DrawWaterNoLight() {
            auto& texture = _baseTexture;
            auto xScale = (float)texture.Info.Width / Resolution;
            auto yScale = (float)texture.Info.Height / Resolution;
            //assert(baseTexture.Info.Width == baseTexture.Info.Height); // Only supports square textures
            // todo: effect clips

            auto& heights = _waterBuffer[_index];

            for (int y = 0; y < Resolution; y++) {
                for (int x = 0; x < Resolution; x++) {
                    int offset = y * Resolution + x;
                    int height = heights[offset];

                    int xHeight;
                    if (x == Resolution - 1)
                        xHeight = heights[offset - Resolution + 1];
                    else
                        xHeight = heights[offset + 1];


                    int yHeight;
                    if (y == Resolution - 1)
                        yHeight = heights[offset - ((Resolution - 1) * Resolution)];
                    else
                        yHeight = heights[offset + Resolution];

                    xHeight = std::max(0, height - xHeight);
                    yHeight = std::max(0, height - yHeight);

                    int xShift = int((xHeight >> 3) + x * xScale) % texture.Info.Width;
                    int yShift = int((yHeight >> 3) + y * yScale) % texture.Info.Width;

                    int destOffset = y * Resolution + x;
                    //int srcOffset = botshift * Resolution + rightshift;
                    int srcOffset = yShift * (int)texture.Info.Width + xShift;

                    _pixels[destOffset] = texture.Data[srcOffset].ToRgba8888();
                }
            }
        }
    };

    Ptr<ProceduralTextureBase> CreateProceduralWater(Outrage::TextureInfo& texture, TexID dest) {
        if (WaterProcTableLo.empty() || WaterProcTableHi.empty())
            InitWaterTables();

        return MakePtr<ProceduralWater>(texture, dest);
    }
}
