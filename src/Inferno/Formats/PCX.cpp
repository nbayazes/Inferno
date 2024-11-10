#include "pch.h"
#include "PCX.h"
#include "Streams.h"

namespace Inferno {
    struct PCXHeader {
        uint8 Manufacturer;
        uint8 Version;
        uint8 Encoding;
        uint8 BitsPerPixel;
        short Xmin;
        short Ymin;
        short Xmax;
        short Ymax;
        short Hdpi;
        short Vdpi;
        uint8 ColorMap[16][3];
        uint8 Reserved;
        uint8 Nplanes;
        short BytesPerLine;
        uint8 padding[60];
    };


    Bitmap2D ReadPCX(span<byte> data) {
        PCXHeader header{};
        StreamReader stream(data);
        stream.ReadBytes(&header, sizeof(header));

        // Is it a 256 color PCX file?
        if (header.Manufacturer != 10 || header.Encoding != 1 || header.Nplanes != 1 || header.BitsPerPixel != 8 || header.Version != 5)
            return {};

        // Find the size of the image
        auto width = header.Xmax - header.Xmin + 1;
        auto height = header.Ymax - header.Ymin + 1;

        List<byte> bitmapData;
        bitmapData.resize(width * height);

        for (int row = 0; row < height; row++) {
            for (int col = 0; col < width;) {
                auto b = stream.ReadByte();

                // Check if RLE
                if ((b & 0xC0) == 0xC0) {
                    int length = b & 0x3F;
                    auto bmpData = stream.ReadByte();

                    for (int i = 0; i < length; i++)
                        bitmapData[width * row + col + i] = bmpData;

                    col += length;
                }
                else {
                    bitmapData[width * row + col] = b;
                    col++;
                }
            }
        }

        struct Rgb { byte r, g, b; };
        Array<Rgb, 256> palette{};

        // Read extended palette at the end of the PCX file
        if (!stream.EndOfStream()) {
            auto b = stream.ReadByte();
            if (b == 12)
                stream.ReadBytes(palette.data(), palette.size() * sizeof(Rgb));

            //for (auto& p : palette)
            //    p >>= 2;
        }

        Bitmap2D bitmap;
        bitmap.Width = width;
        bitmap.Height = height;
        bitmap.Data.resize(width * height);

        for (int i = 0; i < bitmapData.size(); i++) {
            auto& pixel = bitmap.Data[i];
            auto& value = palette[bitmapData[i]];
            pixel.r = value.r;
            pixel.g = value.g;
            pixel.b = value.b;
            //pixel.r = palette[bitmapData[i] * 3 + 0];
            //pixel.g = palette[bitmapData[i] * 3 + 1];
            //pixel.b = palette[bitmapData[i] * 3 + 2];
        }

        return bitmap;
    }
}