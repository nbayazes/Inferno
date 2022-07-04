#include "pch.h"
#include "OutrageBitmap.h"

namespace Inferno {
    enum ImageType {
        OUTRAGE_4444_COMPRESSED_MIPPED = 121, // Only used for textures with specular data
        OUTRAGE_1555_COMPRESSED_MIPPED = 122
    };

    enum BitmapFlag {
        BAD_BITMAP_HANDLE = 0,
        BF_TRANSPARENT = 1,
        BF_CHANGED = 2,
        BF_MIPMAPPED = 4,
        BF_NOT_RESIDENT = 8,
        BF_WANTS_MIP = 16,
        BF_WANTS_4444 = 32,
        BF_BRAND_NEW = 64,
        BF_COMPRESSABLE = 128
    };

    enum BitmapFormat {
        BITMAP_FORMAT_STANDARD = 0,
        BITMAP_FORMAT_1555 = 0,
        BITMAP_FORMAT_4444 = 1,
    };

    constexpr int Conv5to8(int n) { return (n << 3) | (n >> 2); }

    List<uint> Decompress(span<ushort> data, int width, int height, ImageType type) {
        List<uint> img(width * height);
        const int lastrow = (width - 1) * height;

        for (int ofs = 0; ofs <= lastrow; ofs += width) {
            if (type == OUTRAGE_4444_COMPRESSED_MIPPED) {
                for (int x = 0; x < width; x++) {
                    const ushort n = data[ofs + x];
                    //const int a = ((n >> 12) & 0x0f) * 0x11;
                    constexpr int a = 0xff; // ignore alpha for now. it should be extracted as a specular mask
                    const int r = ((n >> 8) & 0x0f) * 0x11;
                    const int g = ((n >> 4) & 0x0f) * 0x11;
                    const int b = (n & 0x0f) * 0x11;
                    img[ofs + x] = a << 24 | b << 16 | g << 8 | r;
                }
            }
            else {
                for (int x = 0; x < width; x++) {
                    const ushort n = data[ofs + x];
                    img[ofs + x] =
                        ((n & 0x8000) * 0x1fe00) |
                        (Conv5to8((n & 0x7c00) >> 10) << 0) |
                        (Conv5to8((n & 0x03e0) >> 5) << 8) |
                        (Conv5to8((n & 0x001f) >> 0) << 16);
                }
            }
        }

        return img;
    }

    OutrageBitmap OutrageBitmap::Read(StreamReader& r) {
        auto imageIdLen = r.ReadByte();
        auto colorMapType = r.ReadByte();
        auto imageType = r.ReadByte();

        if (colorMapType != 0 || (imageType != OUTRAGE_1555_COMPRESSED_MIPPED &&
                                  imageType != OUTRAGE_4444_COMPRESSED_MIPPED))
            throw Exception("Unknown image type");

        OutrageBitmap ogf{};
        ogf.Type = imageType;

        constexpr int BITMAP_NAME_LEN = 35;

        ogf.Name = r.ReadCString(BITMAP_NAME_LEN);
        ogf.MipLevels = r.ReadByte();

        for (int i = 0; i < 9; i++)
            r.ReadByte();

        ogf.Width = r.ReadInt16();
        ogf.Height = r.ReadInt16();
        ogf.BitsPerPixel = r.ReadByte();

        if (ogf.BitsPerPixel != 32 && ogf.BitsPerPixel != 24)
            throw Exception("Invalid BitsPerPixel");

        int descriptor = r.ReadByte();
        if ((descriptor & 0x0F) != 8 && (descriptor & 0x0F) != 0)
            throw Exception("Invalid descriptor");

        for (int i = 0; i < imageIdLen; i++)
            r.ReadByte();

        List<ushort> data(ogf.Width * ogf.Height);

        int count = 0;

        while (count < data.size()) {
            int cmd = r.ReadByte();
            ushort pixel = r.ReadUInt16();

            if (cmd == 0) {
                data[count++] = pixel;
            }
            else if (cmd >= 2 && cmd <= 250) {
                for (int i = 0; i < cmd; i++)
                    data[count++] = pixel;
            }
            else {
                throw Exception("Invalid compression command");
            }
        }

        ogf.Data = Decompress(data, ogf.Width, ogf.Height, (ImageType)ogf.Type);
        return ogf;
    }
}
