#include "pch.h"
#include "BBM.h"
#include "logging.h"
#include "Pig.h"
#include "Streams.h"
#include "Types.h"

// BBM reader for IFF files
namespace Inferno {
    enum class BbmColor { Linear, ModeX, SVGA, RGB15, Palette };

    enum class BbmType : int16 { PBM, ILBM };

    enum class MaskType : int8 { None, Mask, TransparentColor };

    enum class CompressionType : int8 { None, RLE };

    struct IffHeader {
        uint Width, Height;
        BbmType Type;
        int TransparentColor; //which color is transparent (if any)
        int8 Planes; //number of planes (8 for 256 color image)
        MaskType Mask;
        CompressionType Compression;
    };

    // Reads a big endian int16
    int ReadInt16(StreamReader& stream) {
        auto b0 = stream.ReadByte();
        auto b1 = stream.ReadByte();
        return ((int)b0 << 8) + b1;
    }

    // Reads a big endian int32
    int32 ReadInt32(StreamReader& stream) {
        auto b0 = (int)stream.ReadByte();
        auto b1 = (int)stream.ReadByte();
        auto b2 = (int)stream.ReadByte();
        auto b3 = (int)stream.ReadByte();
        return (b0 << 24) + (b1 << 16) + (b2 << 8) + b3;
    }

    List<byte> ParseBody(StreamReader& stream, int32 chunkLen, const IffHeader& header) {
        int width = 0;
        int8 depth = 0;
        int rowCount = 0;
        int offset = 0;

        if (header.Type == BbmType::PBM) {
            width = header.Width;
            depth = 1;
        }
        else if (header.Type == BbmType::ILBM) {
            width = (header.Width + 7) / 89;
            depth = header.Planes;
        }
        else {
            throw Exception("Unknown BBM type");
        }

        List<byte> data;
        data.resize(header.Width * header.Height);
        auto endPosition = stream.Position() + chunkLen;

        if (header.Compression == CompressionType::None) {
            for (int y = header.Height; y; y--) {
                for (int x = 0; x < width * depth; x++)
                    data[offset++] = stream.ReadByte();

                // skip mask
                //if (header.Mask != MaskType::None) stream.SeekForward(width);
                if (header.Width & 1) stream.SeekForward(1); // alignment
            }
        }
        else if (header.Compression == CompressionType::RLE) {
            // NOTE: this code is not tested. No descent BBMs appear to be compressed.
            for (int count = width, plane = 0; count < offset + chunkLen && offset < data.size();) {
                ASSERT(stream.Position() < endPosition);
                auto n = stream.ReadByte();

                if (n < 128) {
                    // uncompressed n + 1 bytes
                    auto nn = n + 1;
                    count -= nn;
                    if (count == -1) {
                        --count;
                        ASSERT(width & 1);
                    }

                    if (plane == depth) {
                        stream.SeekForward(nn); // skip mask
                    }
                    else {
                        while (nn--)
                            data[offset++] = stream.ReadByte();
                    }

                    if (count == -1)
                        stream.SeekForward(1);
                }
                else {
                    auto val = stream.ReadByte();
                    auto len = 257 - n;
                    count -= len;
                    if (count < 0)
                        len--;

                    // not masking row
                    if (plane != depth) {
                        if (offset + len > data.size())
                            throw Exception("BBM data out of range");

                        std::fill_n(data.begin() + offset, len, val);
                        offset += len;
                    }
                }

                if (offset % width == 0)
                    rowCount++;

                ASSERT(offset - (width * rowCount) < width);

                if (header.Type == BbmType::ILBM) {
                    throw NotImplementedException();
                    //ConvertToPbm(data);
                }
            }
        }

        return data;
    }

    Bitmap2D Parse(StreamReader& stream, int type) {
        List<byte> data;
        IffHeader header{};
        header.Type = type == MakeFourCC("PBM ") ? BbmType::PBM : BbmType::ILBM;

        struct PaletteColor {
            uint8 r, g, b;
        };

        Array<PaletteColor, 256> palette{};

        while (!stream.EndOfStream()) {
            auto sig = stream.ReadInt32(); // FourCC flips little-endian to big-endian, using a little-endian read accounts for this
            if (sig == -1) break;

            auto len = ReadInt32(stream);
            if (len == 0) break;

            auto chunkStart = stream.Position();

            switch (sig) {
                case MakeFourCC("BMHD"):
                {
                    // header
                    header.Width = ReadInt16(stream);
                    header.Height = ReadInt16(stream);
                    // skip origin x and y
                    ReadInt16(stream);
                    ReadInt16(stream);

                    header.Planes = stream.ReadByte();
                    if (header.Planes != 8)
                        throw Exception("Planes must equal 8");

                    header.Mask = (MaskType)stream.ReadByte();
                    header.Compression = (CompressionType)stream.ReadByte();
                    if (header.Compression != CompressionType::None && header.Compression != CompressionType::RLE)
                        throw Exception("Unknown compression type");

                    stream.ReadByte(); // padding

                    header.TransparentColor = ReadInt16(stream);

                    // skip aspect ratio x/y
                    stream.ReadByte();
                    stream.ReadByte();

                    // skip page size
                    ReadInt16(stream);
                    ReadInt16(stream);

                    if (header.Mask != MaskType::None && header.Mask != MaskType::TransparentColor)
                        throw Exception("Unsupported mask type");

                    break;
                }

                case MakeFourCC("CMAP"):
                {
                    auto colors = len / 3;

                    for (int i = 0; i < colors; i++) {
                        palette[i].r = stream.ReadByte();
                        palette[i].g = stream.ReadByte();
                        palette[i].b = stream.ReadByte();
                    }

                    break;
                }

                case MakeFourCC("BODY"):
                {
                    data = ParseBody(stream, len, header);
                    break;
                }

                default:
                    break;
            }

            stream.Seek(chunkStart + len);
        }


        Bitmap2D bitmap;
        bitmap.Data.resize(data.size());
        bitmap.Width = header.Width;
        bitmap.Height = header.Height;

        for (size_t i = 0; i < data.size(); i++) {
            if (header.Mask == MaskType::TransparentColor && header.TransparentColor == data[i]) {
                bitmap.Data[i].a = 0;
            }
            else {
                auto& p = palette[data[i]];
                bitmap.Data[i].r = p.r;
                bitmap.Data[i].g = p.g;
                bitmap.Data[i].b = p.b;
            }
        }

        return bitmap;
    }

    Bitmap2D ReadBbm(span<byte> data) {
        try {
            StreamReader stream(data);

            auto id = stream.ReadInt32();
            if (id != MakeFourCC("FORM"))
                throw Exception("Unknown file format");

            ReadInt32(stream); // form length
            auto type = stream.ReadInt32();

            if (type == MakeFourCC("ANIM"))
                throw Exception("Animations are not supported");
            else if (type == MakeFourCC("PBM ") || type == MakeFourCC("ILBM"))
                return Parse(stream, type);
            else
                throw Exception("Unknown file type");
        }
        catch (const Exception& e) {
            SPDLOG_ERROR("BBM/PBM parse error: {}", e.what());
            return {};
        }
    }
}
