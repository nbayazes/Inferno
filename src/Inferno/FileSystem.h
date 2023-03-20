#pragma once

#include "Types.h"

/*
    Locates files on the system from multiple data directories.
*/
namespace Inferno::FileSystem {
    void Init();
    void AddDataDirectory(filesystem::path);
    Option<filesystem::path> TryFindFile(filesystem::path);
    wstring FindFile(filesystem::path);
    List<char> ReadFileBytes(filesystem::path);
    string ReadFileText(filesystem::path);
}
