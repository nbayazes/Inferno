#include "pch.h"
#include "OutrageGraphics.h"

namespace Inferno {
    enum ImageType {
        OUTRAGE_4444_COMPRESSED_MIPPED = 121,
        OUTRAGE_1555_COMPRESSED_MIPPED = 122,
        OUTRAGE_NEW_COMPRESSED_MIPPED = 123,
        OUTRAGE_COMPRESSED_MIPPED = 124,
        OUTRAGE_COMPRESSED_OGF_8BIT = 125,
        OUTRAGE_TGA_TYPE = 126,
        OUTRAGE_COMPRESSED_OGF = 127,
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

    constexpr int BITMAP_NAME_LEN = 35;

    OutrageGraphics OutrageGraphics::Read(StreamReader& r) {
        auto imageIdLen = r.ReadByte();
        auto colorMapType = r.ReadByte();
        auto imageType = r.ReadByte();

        if (colorMapType != 0 || (imageType != 10 &&
                                  imageType != 2 &&
                                  imageType != OUTRAGE_TGA_TYPE &&
                                  imageType != OUTRAGE_COMPRESSED_OGF &&
                                  imageType != OUTRAGE_COMPRESSED_MIPPED &&
                                  imageType != OUTRAGE_NEW_COMPRESSED_MIPPED &&
                                  imageType != OUTRAGE_1555_COMPRESSED_MIPPED &&
                                  imageType != OUTRAGE_4444_COMPRESSED_MIPPED))
            throw Exception("Unknown image type");

        OutrageGraphics ogf{};
        ogf.Type = imageType;

        if (imageType == OUTRAGE_4444_COMPRESSED_MIPPED || 
            imageType == OUTRAGE_1555_COMPRESSED_MIPPED || 
            imageType == OUTRAGE_NEW_COMPRESSED_MIPPED || 
            imageType == OUTRAGE_TGA_TYPE || 
            imageType == OUTRAGE_COMPRESSED_MIPPED || 
            imageType == OUTRAGE_COMPRESSED_OGF || 
            imageType == OUTRAGE_COMPRESSED_OGF_8BIT) {

            if (imageType == OUTRAGE_4444_COMPRESSED_MIPPED ||
                imageType == OUTRAGE_NEW_COMPRESSED_MIPPED ||
                imageType == OUTRAGE_1555_COMPRESSED_MIPPED) {
                ogf.Name = r.ReadCString(BITMAP_NAME_LEN);
            }
            else {
                List<char> buffer(BITMAP_NAME_LEN);
                r.ReadBytes(buffer.data(), BITMAP_NAME_LEN);
                ogf.Name = string(buffer.data()); // probably wrong
                //name = Encoding.UTF8.GetString(r.ReadBytes(BITMAP_NAME_LEN));
            }

            if (imageType == OUTRAGE_4444_COMPRESSED_MIPPED ||
                imageType == OUTRAGE_1555_COMPRESSED_MIPPED ||
                imageType == OUTRAGE_COMPRESSED_MIPPED ||
                imageType == OUTRAGE_NEW_COMPRESSED_MIPPED)
                ogf.MipLevels = r.ReadByte();
        }

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

        ogf.UpsideDown = (descriptor & 0x20) == 0;
        ogf.Data.resize(ogf.Width * ogf.Height);

        if (imageType == OUTRAGE_4444_COMPRESSED_MIPPED ||
            imageType == OUTRAGE_1555_COMPRESSED_MIPPED ||
            imageType == OUTRAGE_NEW_COMPRESSED_MIPPED ||
            imageType == OUTRAGE_COMPRESSED_MIPPED ||
            imageType == OUTRAGE_COMPRESSED_OGF ||
            imageType == OUTRAGE_COMPRESSED_OGF_8BIT) {
            int count = 0;

            while (count < ogf.Data.size()) {
                int cmd = r.ReadByte();
                ushort pixel = r.ReadUInt16();

                if (cmd == 0) {
                    ogf.Data[count++] = pixel;
                }
                else if (cmd >= 2 && cmd <= 250) {
                    for (int i = 0; i < cmd; i++)
                        ogf.Data[count++] = pixel;
                }
                else {
                    throw Exception("Invalid compression command");
                }
            }
        }
        else
            throw Exception("Invalid image file type");

        return ogf;
    }

    constexpr int Conv5to8(int n) { return (n << 3) | (n >> 2); }

    List<int> OutrageGraphics::GetMipData(int /*mip*/) {
        List<int> img(Width * Height);
        int lastrow = (Width - 1) * Height;

        for (int ofs = 0; ofs <= lastrow; ofs += Width) {
            if (Type == OUTRAGE_4444_COMPRESSED_MIPPED) {
                for (int x = 0; x < Width; x++) {
                    ushort n = Data[ofs + x];
                    img[ofs + x] =
                        ((n & 0xf000) * (0x11 << (24 - 12))) |
                        ((n & 0x0f00) * (0x11 << (16 - 8))) |
                        ((n & 0x00f0) * (0x11 << (8 - 4))) |
                        ((n & 0x000f) * (0x11 << 0));
                }
            }
            else {
                for (int x = 0; x < Width; x++) {
                    ushort n = Data[ofs + x];
                    img[ofs + x] =
                        ((n & 0x8000) * 0x1fe00) |
                        (Conv5to8((n & 0x7c00) >> 10) << 16) |
                        (Conv5to8((n & 0x03e0) >> 5) << 8) |
                        (Conv5to8((n & 0x001f) >> 0) << 0);
                }
            }
        }

        return img;
    }



}
