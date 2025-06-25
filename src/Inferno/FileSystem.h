#pragma once

#include "Types.h"

namespace Inferno {
    struct IZipFile {
        virtual span<string> GetEntries() = 0;
        virtual Option<List<ubyte>> TryReadEntry(string_view entryName) const = 0;

        // Returns true if the zip contains the entry
        virtual bool Contains(string_view entryName) const = 0;
        virtual ~IZipFile() = default;

        IZipFile() = default;
        IZipFile(const IZipFile&) = delete;
        IZipFile(IZipFile&&) = default;
        IZipFile& operator=(const IZipFile&) = delete;
        IZipFile& operator=(IZipFile&&) = default;
    };

}

namespace Inferno::File {
    Ptr<IZipFile> OpenZip(const filesystem::path& path);

    // Reads the file at the given path. Throws an exception if not found.
    List<ubyte> ReadAllBytes(const std::filesystem::path& path);
    void WriteAllBytes(const std::filesystem::path& path, span<ubyte> data);
    string ReadAllText(const filesystem::path& path);
    std::vector<std::string> ReadLines(const filesystem::path& path);
    Option<List<byte>> ReadFromZip(const filesystem::path& path, string_view fileName);
}

/*
    Locates files on the system from multiple data directories.
*/
namespace Inferno::FileSystem {
    void Init();
    void AddDataDirectory(const std::filesystem::path&);
    Option<std::filesystem::path> TryFindFile(const std::filesystem::path&);
    filesystem::path FindFile(const std::filesystem::path&);
    span<filesystem::path> GetDirectories();
}
