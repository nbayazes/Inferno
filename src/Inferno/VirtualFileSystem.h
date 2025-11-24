#pragma once

namespace Inferno {
    struct ResourceHandle;
    struct Level;
}

// Inferno virtual file system
namespace Inferno::vfs {
    //void Init();
    //void AddDataDirectory(const std::filesystem::path&);
    //Option<std::filesystem::path> TryFindFile(const std::filesystem::path&);

    // Mounts assets required to display the main menu
    //void MountMainMenu();

    // Mounts custom assets for a level from the filesystem
    //void MountLevel(const Level& level, const filesystem::path& missionPath = {});

    Option<List<ubyte>> ReadAsset(string name);

    // Helper to read image assets based on the extension. Supports DDS, TGA, and WIC formats (PNG).
    //Option<Image> ReadImage(const string& name, bool srgb = true);

    bool AssetExists(string name);
    Option<ResourceHandle> FindAsset(string name);
    //filesystem::path FindFile(const std::filesystem::path&);

    // Mounts a directory, zip, hog. Level name searches for folders inside of zips or directories.
    void Mount(const std::filesystem::path& path, std::initializer_list<string_view> filter = {}, string_view levelName = {});

    // Unmounts all directories and archives
    void Reset();

    // Prints all of the mounted resources
    void Print();

    //void MountGameData(GameData& data);
}
