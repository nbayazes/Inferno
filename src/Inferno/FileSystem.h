#pragma once

#include "Types.h"

namespace Inferno::File {
    // Reads the file at the given path. Throws an exception if not found.
    List<ubyte> ReadAllBytes(const std::filesystem::path& path);
    void WriteAllBytes(const std::filesystem::path& path, span<ubyte> data);
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
