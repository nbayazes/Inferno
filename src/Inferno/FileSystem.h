#pragma once

#include "Pig.h"
#include "Types.h"
#include "Image.h"

namespace Inferno {
    struct ResourceHandle;
    struct Level;

    struct IZipFile {
        virtual span<string> GetEntries() = 0;
        virtual Option<List<ubyte>> TryReadEntry(string_view entryName) const = 0;
        virtual const filesystem::path& Path() const = 0;

        // Returns true if the zip contains the entry
        virtual bool Contains(string_view entryName) const = 0;
        virtual ~IZipFile() = default;

        IZipFile() = default;
        IZipFile(const IZipFile&) = delete;
        IZipFile(IZipFile&&) = default;
        virtual IZipFile& operator=(const IZipFile&) = delete;
        IZipFile& operator=(IZipFile&&) = default;
    };
}

namespace Inferno::File {
    Ptr<IZipFile> OpenZip(const filesystem::path& path);

    // Tries to read an entry from a zip file. Immediately closes the zip afterwards.
    Option<List<ubyte>> ReadZipEntry(const filesystem::path& path, string_view entry);

    // Reads the file at the given path. Throws an exception if not found.
    List<ubyte> ReadAllBytes(const std::filesystem::path& path);
    void WriteAllBytes(const std::filesystem::path& path, span<ubyte> data);
    string ReadAllText(const filesystem::path& path);
    std::vector<std::string> ReadLines(const filesystem::path& path);
}

/*
    Locates files on the system from multiple data directories.
*/
namespace Inferno::FileSystem {
    void Init();
    void AddDataDirectory(const std::filesystem::path&);
    Option<std::filesystem::path> TryFindFile(const std::filesystem::path&);

    // Mounts assets required to display the main menu
    void MountMainMenu();

    // Mounts custom assets for a level from the filesystem
    void MountLevel(const Level& level, const filesystem::path& missionPath = {});

    Option<List<ubyte>> ReadAsset(string name);

    // Helper to read image assets based on the extension. Supports DDS, TGA, and WIC formats (PNG).
    Option<Image> ReadImage(const string& name, bool srgb = true);

    bool AssetExists(string name);
    Option<ResourceHandle> FindAsset(string name);
    filesystem::path FindFile(const std::filesystem::path&);
    span<filesystem::path> GetDirectories();

    // Mounts a directory, zip, hog
    void Mount(const std::filesystem::path& path);
}
