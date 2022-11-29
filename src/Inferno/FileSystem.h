#pragma once

#include "Types.h"

/*
    Locates files on the system from multiple data directories.
*/
namespace Inferno::FileSystem {
    void Init();
    void AddDataDirectory(std::filesystem::path);
    Option<std::filesystem::path> TryFindFile(std::filesystem::path);
    wstring FindFile(std::filesystem::path);
    List<char> ReadFileBytes(std::filesystem::path);
}
