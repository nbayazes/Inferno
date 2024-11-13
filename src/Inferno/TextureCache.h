#pragma once
#include "Pig.h"
#include "HamFile.h"

namespace Inferno {
    // Saves and loads generated textures for specular and normal maps
    class TextureMapCache {
        uint _size = 0;
        Ptr<StreamReader> _stream;
        uint64 _dataStart = 0;

    public:
        struct Entry {
            TexID Id = TexID::None;
            uint64 DataOffset = 0; // data offset from the end of header section
            uint16 Width = 0, Height = 0;
            uint32 DiffuseLength = 0, SpecularLength = 0, NormalLength = 0, MaskLength = 0;
            uint8 Mips = 0;
            List<Palette::Color> Diffuse; //rgb8
            List<ubyte> Specular; // u8
            List<Palette::Color> Normal; //rgb8
            List<ubyte> Mask; // u8, supertransparency mask

            bool IsValid() const { return Mips > 0; }
        };

        List<Entry> Entries;
        filesystem::path Path; // the source path

        void GenerateTextures(const HamFile& ham, const PigFile& pig, const Palette& palette);

        TextureMapCache(filesystem::path path, uint size);

        TextureMapCache() {}

        void Write(const filesystem::path& path);

        // Returns null if the entry doesn't contain any data
        const Entry* GetEntry(TexID id) const {
            auto item = Seq::tryItem(Entries, (int)id);
            return item && item->IsValid() ? item : nullptr;
        }

        const void ReadDiffuseMap(const Entry& entry, List<ubyte>& dest) const {
            _stream->Seek(_dataStart + entry.DataOffset);
            _stream->ReadUBytes(entry.DiffuseLength, dest);
        }

        const void ReadSpecularMap(const Entry& entry, List<ubyte>& dest) const {
            _stream->Seek(_dataStart + entry.DataOffset + entry.DiffuseLength);
            _stream->ReadUBytes(entry.SpecularLength, dest);
        }

        const void ReadNormalMap(const Entry& entry, List<ubyte>& dest) const {
            _stream->Seek(_dataStart + entry.DataOffset + entry.DiffuseLength + entry.SpecularLength);
            _stream->ReadUBytes(entry.NormalLength, dest);
        }

        const void ReadMaskMap(const Entry& entry, List<ubyte>& dest) const {
            _stream->Seek(_dataStart + entry.DataOffset + entry.DiffuseLength + entry.SpecularLength + entry.NormalLength);
            _stream->ReadUBytes(entry.MaskLength, dest);
        }
    private:
        void Deserialize(StreamReader& stream);
    };

    void BuildTextureMapCache();
    void LoadTextureCaches();

    void ExpandMask(const PigEntry& bmp, List<uint8>& data);

    inline TextureMapCache D1TextureCache, D2TextureCache;
}
