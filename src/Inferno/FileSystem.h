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
    void AddDataDirectory(const filesystem::path&);
    Option<filesystem::path> TryFindFile(const filesystem::path&);
    wstring FindFile(const filesystem::path&);
    string ReadFileText(const filesystem::path&);
}
