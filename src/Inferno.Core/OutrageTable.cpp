#include "pch.h"
#include "OutrageTable.h"

namespace Inferno::Outrage {
    constexpr auto PAGENAME_LEN = 35;

    enum PageType {
        PAGETYPE_TEXTURE = 1,
        PAGETYPE_DOOR = 5,
        PAGETYPE_SOUND = 7,
        PAGETYPE_GENERIC = 10,
    };

    constexpr int KNOWN_TEXTURE_VERSION = 7;
    constexpr int MAX_STRING_LEN = 256;

    TextureInfo ReadTexturePage(StreamReader& r) {
        auto version = r.ReadInt16();
        if (version > KNOWN_TEXTURE_VERSION)
            throw Exception("Unsupported texture info version");

        TextureInfo tex;
        tex.Name = r.ReadCString(MAX_STRING_LEN);
        tex.FileName = r.ReadCString(MAX_STRING_LEN);
        r.ReadCString(MAX_STRING_LEN);
        tex.Color.x = r.ReadFloat();
        tex.Color.y = r.ReadFloat();
        tex.Color.z = r.ReadFloat();
        tex.Color.w = r.ReadFloat();

        tex.Speed = r.ReadFloat();
        tex.Slide.x = r.ReadFloat();
        tex.Slide.y = r.ReadFloat();
        tex.Reflectivity = r.ReadFloat();

        r.ReadByte();
        r.ReadInt32();

        tex.Flags = (TextureFlag)r.ReadInt32();
        if ((tex.Flags & TF_PROCEDURAL) != 0) {
            for (int i = 0; i < 255; i++)
                r.ReadInt16();
            r.ReadByte();
            r.ReadByte();
            r.ReadByte();
            r.ReadFloat();
            if (version >= 6) {
                r.ReadFloat();
                r.ReadByte();
            }
            int n = r.ReadInt16();
            for (int i = 0; i < n; i++) {
                r.ReadByte();
                r.ReadByte();
                r.ReadByte();
                r.ReadByte();
                r.ReadByte();
                r.ReadByte();
                r.ReadByte();
                r.ReadByte();
            }
        }

        if (version >= 5) {
            if (version < 7)
                r.ReadInt16();
            else
                r.ReadCString(MAX_STRING_LEN);
            r.ReadFloat();
        }
        return tex;
    }

    GameTable GameTable::Read(StreamReader& r) {
        GameTable table{};

        while (!r.EndOfStream()) {
            auto pageType = r.ReadByte();
            auto len = r.ReadInt32();
            if (len == 0) break;
            auto nextChunk = r.Position() + 1 + len;

            switch (pageType) {
                case PAGETYPE_TEXTURE:
                    table.Textures.push_back(ReadTexturePage(r));
                    break;

                default:
                    r.SeekForward(nextChunk);
                    break;
            }
        }

        return table;
    }
}
