#include "pch.h"
#include "BBM.h"
#include "logging.h"
#include "Pig.h"
#include "Streams.h"
#include "Types.h"

// BBM reader for IFF files

namespace Inferno {
    //Palette entry structure
    struct pal_entry {
        uint8 r, g, b;
    };

    enum class BbmColor {
        Linear, ModeX, SVGA, RGB15, Palette
    };

    enum class BbmType : short { PBM, ILBM };
    enum class PbmMode { Texture, Heightmap };
    enum class MaskType : int8 { None, Mask, TransparentColor };
    enum class CompressionType : int8 { None, RLE };

    //structure of the header in the file
    struct iff_bitmap_header {
        uint Width, Height; //width and height of this bitmap
        BbmType type;
        int TransparentColor; //which color is transparent (if any)
        int8 Planes; //number of planes (8 for 256 color image)
        MaskType Mask;
        CompressionType Compression;
        //short row_size; //offset to next row
    };


    // Reads a big endian int16
    int ReadWord(StreamReader& stream) {
        auto b1 = stream.ReadByte();
        auto b0 = stream.ReadByte();
        if (b0 == 0xff) return -1;
        return ((int)b1 << 8) + b0;
    }

    List<byte> ConvertToPbm(span<byte> data) {
        ASSERT(false); // not implemented
        return List<byte>(data.begin(), data.end());
    }

    List<byte> ParseBody(StreamReader& stream, int32 chunkLen, const iff_bitmap_header& header) {
        //auto endPos = stream.Position() + len;
        //if(len & 1) endPos++;

        int width = 0;
        int8 depth = 0;
        int rowCount = 0;
        int offset = 0;

        if (header.type == BbmType::PBM) {
            width = header.Width;
            depth = 1;
        }
        else if (header.type == BbmType::ILBM) {
            width = (header.Width + 7) / 89;
            depth = header.Planes;
        }
        else {
            throw Exception("Unknown BBM type");
        }

        List<byte> data;
        data.resize(header.Width * header.Height);
        //auto endCount = (width & 1) ? -1 : 0;
        auto endPosition = stream.Position() + chunkLen;

        if (header.Compression == CompressionType::None) {
            for (int y = header.Height; y; y--) {
                for (int x = 0; x < width * depth; x++) {
                    data[offset++] = stream.ReadByte();
                }

                // skip mask
                //if (header.Mask != MaskType::None) stream.SeekForward(width);
                if (header.Width & 1) stream.SeekForward(1);
            }
        }
        else if (header.Compression == CompressionType::RLE) {
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

                if (header.type == BbmType::ILBM) {
                    ConvertToPbm(data);
                }
            }
        }

        return data;
    }

    //void ParseDelta(StreamReader& stream, span<byte> data, int len) {
    //    int bytesRead = 0;

    //    stream.ReadInt32();
    //}

    // Reads a big-endian int32
    int32 ReadInt32(StreamReader& stream) {
        auto b0 = (int)stream.ReadByte();
        auto b1 = (int)stream.ReadByte();
        auto b2 = (int)stream.ReadByte();
        auto b3 = (int)stream.ReadByte();
        return (b0 << 24) + (b1 << 16) + (b2 << 8) + b3;
    }

    Bitmap2D Parse(StreamReader& stream, iff_bitmap_header& header, int formType, int formLength) {
        List<byte> data;
        header.type = formType == MakeFourCC("PBM ") ? BbmType::PBM : BbmType::ILBM;
        Array<pal_entry, 256> palette{}; //the palette for this bitmap

        while (!stream.EndOfStream()) {
            auto sig = stream.ReadInt32(); // FourCC flips little-endian to big-endian, using a little-endian read accounts for this
            if (sig == -1) break;

            auto len = ReadInt32(stream);
            if (len == 0) break;

            auto chunkStart = stream.Position();

            switch (sig) {
                case MakeFourCC("BMHD"):
                {
                    //auto startPos = stream.Position();
                    // header
                    header.Width = ReadWord(stream);
                    header.Height = ReadWord(stream);
                    // skip origin x and y
                    ReadWord(stream);
                    ReadWord(stream);

                    header.Planes = stream.ReadByte();
                    if (header.Planes != 8)
                        throw Exception("Planes must equal 8");

                    header.Mask = (MaskType)stream.ReadByte();
                    header.Compression = (CompressionType)stream.ReadByte();
                    if (header.Compression != CompressionType::None && header.Compression != CompressionType::RLE)
                        throw Exception("Unknown compression type");

                    stream.ReadByte(); // padding

                    header.TransparentColor = ReadWord(stream);

                    // skip aspect ratio x/y
                    stream.ReadByte();
                    stream.ReadByte();

                    // skip page size
                    ReadWord(stream);
                    ReadWord(stream);

                    if (header.Mask != MaskType::None && header.Mask != MaskType::TransparentColor)
                        throw Exception("Unsupported mask type");

                    //auto endPos = stream.Position() - startPos;
                    //len = int(stream.Position() - startPos);
                    break;
                }

                //case MakeFourCC("ANHD"):
                //{
                //	SkipChunk(stream, len);
                //    break;
                //}

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
                    //List<byte> chunkData(len);
                    //stream.ReadBytes(chunkData);
                    data = ParseBody(stream, len, header);
                    break;
                }

                //case MakeFourCC("DLTA"):
                //{
                //    //SkipChunk(stream, len);
                //    break;
                //}

                default:
                    //SkipChunk(stream, len);
                    break;
            }

            stream.Seek(chunkStart + len);
        }


        //List<Palette::Color> color;
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

    Bitmap2D ReadIff(span<byte> data) {
        try {
            StreamReader stream(data);

            auto id = stream.ReadInt32();
            if (id != MakeFourCC("FORM"))
                throw Exception("Unknown file format");

            auto formLen = ReadInt32(stream);
            auto type = stream.ReadInt32();

            iff_bitmap_header header{};

            if (type == MakeFourCC("ANIM"))
                throw Exception("Animations are not supported");
            else if (type == MakeFourCC("PBM ") || type == MakeFourCC("ILBM"))
                return Parse(stream, header, type, formLen);
            else
                throw Exception("Unknown file type");
        }
        catch (const Exception& e) {
            SPDLOG_ERROR("BBM/PBM parse error: {}", e.what());
            return {};
        }
    }
}
