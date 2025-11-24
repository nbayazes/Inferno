#include "pch.h"
#include "VirtualFileSystem.h"
#include "FileSystem.h"
#include "Hog.IO.h"
#include "Hog2.h"
#include "Resources.Common.h"
#include "SystemClock.h"
#include "unordered_dense.h"
#include "Utility.h"
#include "ZipFile.h"

namespace Inferno::vfs {
    namespace {
        ankerl::unordered_dense::map<string, ResourceHandle> Assets;
        List<filesystem::path> Directories;
    }

    // Returns true if filter is empty or value exists in the filter
    bool PassesFilter(string_view value, std::initializer_list<string_view> filter) {
        if (filter.size() == 0) return true; // always passes filter if there isn't one

        for (auto f : filter) {
            if (String::InvariantEquals(value, f))
                return true;
        }

        return false;
    }

    // Mounts a zip file, skipping any subfolders except for special ones. If level name is provided that folder is also added.
    void MountZip(const std::filesystem::path& path, string_view levelName) {
        auto zip = OpenZip(path);

        if (!zip) {
            SPDLOG_WARN("Unable to open zip {}", path.string());
            return;
        }

        SPDLOG_INFO("Mounting zip: {}", path.string());

        auto levelFolder = String::NameWithoutExtension(levelName) + "/";

        for (auto& entry : zip->GetEntries()) {
            if (entry.ends_with("/")) continue; // skip folders

            auto key = String::ToLower(entry);

            auto specialFolder =
                key.starts_with("models") ||
                key.starts_with("textures") ||
                key.starts_with("sounds") ||
                key.starts_with("music");

            // Skip any folders that are not a special folder
            if (String::Contains(key, "/") && !specialFolder) continue;

            auto fileName = filesystem::path{ key }.filename().string();
            Assets[key] = ResourceHandle::FromZip(path, fileName);
            //SPDLOG_INFO("Mounting {}", fileName);
        }

        for (auto& entry : zip->GetEntries()) {
            if (entry.ends_with("/")) continue; // skip folders

            auto key = String::ToLower(entry);
            if (!String::Contains(key, levelFolder)) continue; // skip non level files

            // Add all subfolders in a level folder
            auto fileName = filesystem::path{ key }.filename().string();
            Assets[key] = ResourceHandle::FromZip(path, fileName);
            //SPDLOG_INFO("Mounting {}", fileName);
        }
    }

    // Mounts the contents of a hog, hog2, zip, or dxa
    bool MountArchive(const std::filesystem::path& path, std::initializer_list<string_view> filter, string_view levelName) {
        auto ext = path.extension().string();
        if (!PassesFilter(ext, filter)) return false;

        if (String::InvariantEquals(ext, ".hog")) {
            // try mounting a D1, D2, or D3 hog

            if (HogFile::IsHog(path)) {
                SPDLOG_INFO("Mounting D1/D2 hog: {}", path.string());
                auto prefix = "";

                if (String::ToLower(path.parent_path().string()).starts_with("d1")) {
                    prefix = "d1:";
                }
                else if (String::ToLower(path.parent_path().string()).starts_with("d2")) {
                    prefix = "d2:";
                }

                HogReader reader(path);
                for (auto& entry : reader.Entries()) {
                    auto key = String::ToLower(entry.Name);

                    // Add the resource twice, once with scope prefix and once as a global resource
                    Assets[prefix + key] = ResourceHandle::FromHog(path, entry.Name);
                    Assets[key] = ResourceHandle::FromHog(path, entry.Name);
                }

                return true;
            }
            else if (Hog2::IsHog2(path)) {
                SPDLOG_INFO("Mounting D3 hog: {}", path.string());
                auto hog = Hog2::Read(path);
                auto prefix = "d3:";

                for (auto& entry : hog.Entries) {
                    auto key = String::ToLower(entry.name);
                    Assets[prefix + key] = ResourceHandle::FromHog(path, entry.name);
                    Assets[key] = ResourceHandle::FromHog(path, entry.name);
                }

                return true;
            }
            else {
                SPDLOG_WARN("Tried to read unknown hog type: {}", path.string());
            }
        }
        else if (String::InvariantEquals(ext, ".zip") || String::InvariantEquals(ext, ".dxa")) {
            MountZip(path, levelName);
            return true;
        }

        return false;
    }

    void MountDirectory(const std::filesystem::path& path, bool includeSpecialFolders, std::initializer_list<string_view> filter, string_view levelName) {
        if (!filesystem::exists(path) || !filesystem::is_directory(path)) return;

        SPDLOG_INFO("Mounting directory: {}", path.string());

        // mount hogs first, as they are lowest priority
        for (auto& entry : filesystem::directory_iterator(path)) {
            auto ext = entry.path().extension().string();

            if (PassesFilter(ext, { ".hog" })) {
                MountArchive(entry.path(), filter, levelName);
            }
        }

        // mount dxas before loose files
        for (auto& entry : filesystem::directory_iterator(path)) {
            auto ext = entry.path().extension().string();

            if (PassesFilter(ext, { ".dxa" })) {
                MountArchive(entry.path(), filter, levelName);
            }
        }

        // add loose files
        for (auto& entry : filesystem::directory_iterator(path)) {
            if (!entry.is_directory()) {
                auto ext = entry.path().extension().string();

                if (PassesFilter(ext, { ".hog", ".dxa" })) {
                    continue; // dxas and hogs were handled above
                }

                if (!PassesFilter(ext, filter))
                    continue; // didn't pass filter

                if (PassesFilter(ext, { ".bak", ".sav" })) {
                    continue; // skip editor save files
                }

                if (PassesFilter(ext, { ".zip" })) {
                    MountArchive(entry.path(), filter, levelName);
                    continue;
                }

                auto key = String::ToLower(entry.path().filename().string());

                if (Assets.contains(key)) {
                    SPDLOG_INFO("Updating {} to {}", key, entry.path().string());
                }

                auto lowerPath = String::ToLower(path.string());
                string prefix;

                if (lowerPath.starts_with("d1"))
                    prefix = "d1:";
                else if (lowerPath.starts_with("d2"))
                    prefix = "d2:";
                else if (lowerPath.starts_with("d3"))
                    prefix = "d3:";

                if (!prefix.empty())
                    Assets[prefix + key] = ResourceHandle::FromFilesystem(entry.path());

                Assets[key] = ResourceHandle::FromFilesystem(entry.path());
            }
        }

        // add subdirectories
        for (auto& entry : filesystem::directory_iterator(path)) {
            if (entry.is_directory()) {
                // mount files in special directories
                auto folder = entry.path().filename().string(); // filename returns the folder 

                if (includeSpecialFolders && PassesFilter(folder, { "models", "textures", "sounds", "music" }))
                    MountDirectory(entry.path(), false, filter, {});

                if (!levelName.empty())
                    MountDirectory(entry.path() / levelName, true, filter, {});
            }
        }
    }

    Option<ResourceHandle> FindAsset(string name) {
        name = String::ToLower(name);
        auto resource = Assets.find(name);
        return resource != Assets.end() ? Option(resource->second) : std::nullopt;
    }

    Option<List<ubyte>> ReadAsset(string name) {
        name = String::ToLower(name);

        if (auto resource = Assets.find(name); resource != Assets.end()) {
            auto& asset = resource->second;
            switch (asset.source) {
                case Filesystem:
                    return File::ReadAllBytes(asset.path);
                case Hog:
                {
                    HogReader hog(asset.path);
                    return hog.TryReadEntry(name);
                }
                case Zip:
                {
                    if (auto zip = OpenZip(asset.path)) {
                        return zip->TryReadEntry(asset.name);
                    }
                    else {
                        SPDLOG_ERROR("Unable to read {} from {}", name, asset.path.string());
                    }
                    break;
                }
                default:
                {
                    SPDLOG_WARN("Unknown asset source {}:{}", (int)asset.source, asset.name);
                }
            }
        }

        //SPDLOG_WARN("Asset not found {}", name);
        return {};
    }

    bool AssetExists(string name) {
        name = String::ToLower(name);
        auto resource = Assets.find(name);
        return resource != Assets.end();
    }

    void Mount(const std::filesystem::path& path, std::initializer_list<string_view> filter, string_view levelName) {
        //auto start = Clock.GetTotalTimeSeconds();

        if (path.empty()) return;

        if (is_directory(path)) {
            MountDirectory(path, true, filter, levelName);
        }
        else {
            MountArchive(path, filter, levelName);
        }

        //SPDLOG_INFO("Mounted {} in {:.2f}s", path.string(), Clock.GetTotalTimeSeconds() - start);
    }

    void Reset() {
        Assets.clear();
    }

    void Print() {
        for (auto& [key, value] : Assets)
            SPDLOG_INFO("{} - {}", key, value.path.string());
    }
}
