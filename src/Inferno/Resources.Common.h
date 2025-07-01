#pragma once
#include "HamFile.h"
#include "HogFile.h"

namespace Inferno {
    constexpr auto METADATA_EXTENSION = ".ied"; // inferno engine data
    constexpr auto LIGHT_TABLE_EXTENSION = ".lig"; // level specific light table (when packed in mission)
    constexpr auto MATERIAL_TABLE_EXTENSION = ".ma"; // level specific material extension. Mission and global material tables are always named material.yml
    inline const filesystem::path ASSET_FOLDER = "assets"; // subdirectory containing shared data
    inline const filesystem::path D1_FOLDER = "d1"; // subdirectory containing the d1 hog and pig
    inline const filesystem::path D1_DEMO_FOLDER = "d1/demo"; // subdirectory containing the d1 demo hog and pig
    inline const filesystem::path D2_FOLDER = "d2"; // subdirectory containing the d2 hog and pig
    inline const filesystem::path D1_MATERIAL_FILE = D1_FOLDER / "material.yml";
    inline const filesystem::path D2_MATERIAL_FILE = D2_FOLDER / "material.yml";
    constexpr auto GAME_TABLE_FILE = "game.yml";
    constexpr auto LIGHT_TABLE_FILE = "lights.yml";

    // Where to load a table file from (lights, materials and game table)
    enum class TableSource { Undefined, Descent1, Descent2, Mission, Level, Descent3 };

    struct FullGameData : HamFile {
        SoundFile sounds;
        HogFile hog; // Archive
        Palette palette;
        PigFile pig; // texture headers and data
        List<PigBitmap> bitmaps; // loaded texture data

        enum Source {
            Unknown, Descent1, Descent1Demo, Descent2
        };

        Source source = Unknown;

        FullGameData() = default;
        explicit FullGameData(const HamFile& ham, Source source) : HamFile(ham), source(source) {}
    };

    // Resource load flags for finding data sources
    enum class LoadFlag {
        None = 0,
        Descent1 = 1 << 1, // Search D1 data folder or hog
        Descent2 = 1 << 2, // Search D2 data folder or hog
        Descent3 = 1 << 3, // Search D3 hog
        Filesystem = 1 << 4, // Search the filesystem for loose files. Requires D1, D2, D3 or Common to be set.
        Mission = 1 << 5, // search the currently loaded mission. Also implies the 'unpacked' system folder adjacent to the mission file and the addon zip.
        Dxa = 1 << 6, // search DXAs in the D1, D2, or data folder
        BaseHog = 1 << 7, // descent1.hog or descent2.hog. Only valid when Descent1 or Descent2 is also set.
        Common = 1 << 8, // Search common asset folder
        Texture = 1 << 9, // Search `textures` subfolder
        Sound = 1 << 10, // Search `sounds` subfolder
        Model = 1 << 11, // Search `models` subfolder
        Music = 1 << 12, // Search `music` subfolder
        Level = 1 << 13, // Search the level specific subfolder
        LevelType = 1 << 14, // Adds the Descent1 or Descent2 flag based on the current level
        Default = Mission | Dxa | Filesystem | BaseHog | Common
    };

    struct Level;

    LoadFlag GetLevelLoadFlag(const Level& level);

    enum Source {
        Filesystem, Hog, Zip
    };

    struct ResourceHandle {
        //ResourceHandle(filesystem::path path, string_view name, Source source)
        //    : path(std::move(path)), name(string(name)), source(source) {
        //    // if path matches the filename it is an unpackaged file
        //    if (!path.has_filename()) path /= name; // append filename to path
        //}

        static ResourceHandle FromHog(const filesystem::path& path, string_view name) {
            return {
                .path = path,
                .name = string(name),
                .source = Source::Hog
            };
        }

        static ResourceHandle FromZip(const filesystem::path& path, string_view name) {
            return {
                .path = path,
                .name = string(name),
                .source = Source::Zip
            };
        }

        static ResourceHandle FromFilesystem(const filesystem::path& path) {
            return {
                .path = path,
                //.name = string(name),
                .source = Source::Filesystem
            };
        }

        filesystem::path path; // path on filesystem
        string name; // resource name
        Source source;
    };
}
