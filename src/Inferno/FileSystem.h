#pragma once

#include "Types.h"

namespace Inferno::File {
    // Reads the file at the given path. Throws an exception if not found.
    List<ubyte> ReadAllBytes(std::filesystem::path path);
    void WriteAllBytes(std::filesystem::path path, span<ubyte> data);
}

/*
    Locates files on the system from multiple data directories.
*/
namespace Inferno::FileSystem {
    void Init();
    void AddDataDirectory(std::filesystem::path);
    Option<std::filesystem::path> TryFindFile(std::filesystem::path);
    wstring FindFile(std::filesystem::path);
}
