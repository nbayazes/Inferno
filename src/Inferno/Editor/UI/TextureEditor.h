#pragma once
#include "Game.h"
#include "Graphics.h"
#include "WindowBase.h"
#include "Resources.h"

namespace Inferno::Editor {
    enum class BitmapTransparencyMode {
        NoTransparency,
        ByPaletteIndex,
        ByColor
    };

    void WriteBmp(const filesystem::path& path, const Palette& gamePalette, const PigBitmap& bmp);

    // A user defined texture in a POG or DTX
    struct CustomTexture : PigEntry {
        //List<ubyte> Data; // Indexed color data
        List<Palette::Color> Data;
    };

    // Editor for importing custom textures
    class TextureEditor final : public WindowBase {
        bool _showModified = true;
        bool _showLevel = false;
        bool _showPowerups = false;
        bool _showRobots = false;
        bool _showMisc = false;
        bool _showInUse = true;
        TexID _selection = TexID{ 1 };
        bool _useTransparency = false;
        bool _whiteAsTransparent = false;
        Set<TexID> _levelTextures;
        List<TexID> _visibleTextures;
        bool _initialized = false;
        List<char> _search;

    public:
        TextureEditor();

    protected:
        void OnUpdate() override;

    private:
        void OnImport(const PigEntry& entry);

        void OnRevert(TexID id) {
            if (Resources::CustomTextures.Get(id)) {
                Resources::CustomTextures.Delete(id);
                std::array ids{ id };
                Graphics::LoadMaterialsAsync(ids, true);
                UpdateTextureList();
            }
        }

        void UpdateTextureList();
    };
}
