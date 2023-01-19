#pragma once
#include "Pig.h"

namespace Inferno {
    enum class TextureType {
        Level,
        Robot,
        Powerup,
        Misc
    };

    TextureType ClassifyTexture(const PigEntry& entry);

    class CustomTextureLibrary {
        Dictionary<TexID, PigBitmap> _textures;
    public:
        void Delete(TexID id) {
            if (_textures.contains(id))
                _textures.erase(id);
        }

        const PigBitmap* Get(TexID id) {
            if (_textures.contains(id))
                return &_textures[id];

            return nullptr;
        }

        bool Any() const { return !_textures.empty(); }
        void Clear() { _textures.clear(); }
        void ImportBmp(const filesystem::path& path, bool transparent, PigEntry entry, bool descent1);
        size_t WritePog(StreamWriter&, const Palette&);
        size_t WriteDtx(StreamWriter&, const Palette&);

        //std::vector<ubyte> SerializePog(const Palette& palette) {
        //    SerializeToMemory([&palette, this](StreamWriter& writer) { return WritePog(writer, palette); });
        //}

        // Loads a POG and updates the PIG entry table.
        void LoadPog(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette);

        // Loads a DTX and updates the PIG entry table.
        // DTX patches are similar to POGs, but for D1.
        void LoadDtx(span<PigEntry> pigEntries, span<ubyte> data, const Palette& palette);

    private:
        List<TexID> GetSortedIds() {
            List<TexID> ids;
            ids.reserve(_textures.size());
            for (auto& id : _textures | views::keys)
                ids.push_back(id);

            Seq::sort(ids);
            return ids;
        }
    };
}
