#pragma once

namespace Inferno {
    struct ResourceHandle;
}

/* Inferno virtual file system
 *
 * The VFS indexes files from multiple sources into a dictionary using the file name as the key.
 * Duplicate file names replace earlier entries, even if they are in a different folder.
 * This design is due to D1, D2 and D3 not supporting resource paths.
 *
 * "models", "textures", "sounds", and "music" are special subfolders that are also
 * indexed when mounting a folder or archive. A folder matching the current level file name
 * is also indexed. All other subfolders are ignored.
 *
 * Assets are prefixed with d1: d2: or d3: depending on their source, in addition to adding the
 * un-prefixed version to the dictionary. This is so game specific assets can be referenced outside
 * of the current game. For example D1 robot sounds in D2.
 *
 * Assets are mounted in the following order:
 * - Base d1/descent.hog or d2/descent2.hog
 * - d1/*.dxa or d2/*.dxa archives (Rebirth addon data, high res fonts and backgrounds)
 * - assets/
 * - d1/ loose files
 * - descent3 hog (if enabled)
 * - mods
 * - level/ (for unpacked levels)
 * - mission.hog (for missions)
 * - mission.zip
 * - mission.zip/level
 * - mission/mission (unpacked assets get priority)
 * - mission/mission/level
 */
namespace Inferno::vfs {
    // Tries to read a resource. Supports comma separated resource names.
    Option<List<ubyte>> Read(string_view name);

    // Reads a resource handle
    Option<List<ubyte>> Read(const ResourceHandle& resource);

    // Tries to find a resource. Supports comma separated resource names.
    Option<ResourceHandle> Find(string_view name);

    // Mounts a directory, zip, hog or file. Level name is used to search for folders inside of zips or directories.
    void Mount(const std::filesystem::path& path, std::initializer_list<string_view> filter = {}, string_view levelName = {});

    // Unmounts all directories and archives
    void Unmount();

    // Prints all of the mounted resources
    void Print();
}

