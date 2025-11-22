#include "pch.h"
#include "FileSystem.h"
#include <DirectXTex.h>
#include <fstream>
#include <ranges>
#include <unordered_dense.h>
#include <zip/zip.h>
#include "Game.h"
#include "Hog.IO.h"
#include "Hog2.h"
#include "Logging.h"
#include "Mods.h"
#include "Resources.h"
#include "Settings.h"

namespace Inferno {
    namespace {
        bool ResizeImage(const DirectX::Image& src, DirectX::ScratchImage& dest, bool wrapU, bool wrapV, uint8 width = 64, uint8 height = 64) {
            using namespace DirectX;
            auto flags = TEX_FILTER_DEFAULT;
            if (wrapU) flags |= TEX_FILTER_WRAP_U;
            if (wrapV) flags |= TEX_FILTER_WRAP_V;

            if (SUCCEEDED(Resize(src, width, height, flags, dest)))
                return true;

            return false;
        }

        //void CopyScratchImageToBitmap(const DirectX::Image& image, PigBitmap& dest) {
        //    dest.Data.resize(image.slicePitch / 4);
        //    memcpy(dest.Data.data(), image.pixels, image.slicePitch);
        //    dest.Info.Width = (uint16)image.width;
        //    dest.Info.Height = (uint16)image.height;
        //}

        //bool LoadPng(span<ubyte> png, DirectX::ScratchImage& result, DirectX::TexMetadata& metadata, bool srgb) {
        //    size_t rowPitch, slicePitch;
        //    List<uint8> pixels;

        //    auto flags = srgb ? DirectX::WIC_FLAGS_DEFAULT_SRGB : DirectX::WIC_FLAGS_FORCE_LINEAR;

        //    DirectX::ScratchImage pngData, premultiplied;
        //    if (!SUCCEEDED(DirectX::LoadFromWICMemory(png.data(), png.size(), flags, &metadata, pngData)))
        //        return false;

        //    auto format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

        //    if (!SUCCEEDED(DirectX::ComputePitch(format, metadata.width, metadata.height, rowPitch, slicePitch)))
        //        return false;

        //    DirectX::Image image(metadata.width, metadata.height, format, rowPitch, slicePitch, pngData.GetPixels());

        //    return SUCCEEDED(PremultiplyAlpha(image, DirectX::TEX_PMALPHA_DEFAULT, result));
        //}


        //bool LoadDDS(string_view filename, PigBitmap& dest, bool wrapU, bool wrapV, uint8 size = 64) {
        //    using namespace DirectX;
        //    ScratchImage dds, decompressed, resized;
        //    TexMetadata metadata;

        //    auto data = Inferno::FileSystem::ReadAsset(string(filename));
        //    if (!data) return false;

        //    if (FAILED(LoadFromDDSMemory(data->data(), data->size(), DDS_FLAGS_NONE, &metadata, dds)))
        //        return false;

        //    if (FAILED(Decompress(*dds.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, decompressed)))
        //        return false;

        //    auto image = decompressed.GetImage(0, 0, 0);

        //    if (metadata.width != size || metadata.height != size) {
        //        if (ResizeImage(*image, resized, wrapU, wrapV, size))
        //            image = resized.GetImage(0, 0, 0);
        //    }

        //    CopyScratchImageToBitmap(*image, dest);

        //    
        //    return true;
        //}
    }


    class ZipFile final : public IZipFile {
        zip_t* _zip = nullptr;
        List<string> _entries;
        filesystem::path _path;

    public:
        static Ptr<IZipFile> Open(const filesystem::path& path) {
            auto zip = zip_open(path.string().c_str(), 0, 'r');
            if (!zip) return {};

            auto file = std::make_unique<ZipFile>();

            file->_zip = zip;
            file->_path = path;

            auto totalEntries = zip_entries_total(zip);
            List<string> fileNames;

            // index the entries
            for (int i = 0; i < totalEntries; i++) {
                if (zip_entry_openbyindex(zip, i) < 0)
                    break;

                string name = zip_entry_name(zip);
                file->_entries.push_back(name);
                zip_entry_close(zip);
            }

            return file;
        }

        span<string> GetEntries() override { return _entries; }

        bool Contains(string_view fileName) const override {
            return Seq::contains(_entries, fileName);
        }

        /*ResourceHandle Find(string_view fileName) {
            return Seq::tryItem(_entries, fileName);
        }*/

        Option<List<ubyte>> TryReadEntry(string_view fileName) const override try {
            List<byte> data;
            if (zip_entry_open(_zip, string(fileName).c_str()) == 0) {
                void* buffer;
                size_t bufferSize;
                auto readBytes = zip_entry_read(_zip, &buffer, &bufferSize);
                if (readBytes > 0) {
                    //SPDLOG_INFO("Read file from {}:{}", _path.string(), fileName);
                    data.assign((byte*)buffer, (byte*)buffer + bufferSize);
                }

                zip_entry_close(_zip);
            }

            if (data.empty())
                return {};

            return data;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error reading {}: {}", _path.string(), e.what());
            return {};
        }

        ZipFile(const ZipFile&) = delete;
        ZipFile(ZipFile&&) = default;
        ZipFile& operator=(const ZipFile&) = delete;
        ZipFile& operator=(ZipFile&&) = default;

        ~ZipFile() override {
            zip_close(_zip);
        }

        ZipFile() {}

        const filesystem::path& Path() const override { return _path; }
    };

    List<ubyte> File::ReadAllBytes(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            auto msg = fmt::format("Required file not found:\n{}", path.string());
            throw Exception(msg);
        }

        auto size = filesystem::file_size(path);
        List<ubyte> buffer(size);
        if (!file.read((char*)buffer.data(), size)) {
            auto msg = fmt::format("File read error: {}", path.string());
            throw Exception(msg);
        }

        return buffer;
    }

    void File::WriteAllBytes(const std::filesystem::path& path, span<ubyte> data) {
        std::ofstream file(path, std::ios::binary);
        StreamWriter writer(file, false);
        writer.WriteBytes(data);
        SPDLOG_INFO("Wrote {} bytes to {}", data.size(), path.string());
    }

    std::string File::ReadAllText(const filesystem::path& path) {
        std::ifstream stream(path);
        if (!stream) {
            SPDLOG_WARN("Unable to open file `{}`", path.string());
            return {};
        }

        return { std::istreambuf_iterator(stream), std::istreambuf_iterator<char>() };
    }

    std::vector<std::string> File::ReadLines(const filesystem::path& path) {
        std::ifstream stream(path);
        if (!stream) {
            SPDLOG_WARN("Unable to open file `{}`", path.string());
            return {};
        }

        std::vector<std::string> lines;
        std::string line;

        while (std::getline(stream, line))
            lines.push_back(line);

        return lines;
    }

    Ptr<IZipFile> File::OpenZip(const filesystem::path& path) {
        return ZipFile::Open(path);
    }

    Option<List<ubyte>> File::ReadZipEntry(const filesystem::path& path, string_view entry) {
        if (auto zip = File::OpenZip(path)) {
            return zip->TryReadEntry(entry);
        }

        return {};
    }
}

namespace Inferno::FileSystem {
    namespace {
        ankerl::unordered_dense::map<string, ResourceHandle> Assets;
        List<filesystem::path> Directories;
    }

    filesystem::path FindFile(const filesystem::path& file) {
        if (auto path = TryFindFile(file))
            return *path;

        auto msg = fmt::format("File not found: {}", file.string());
        SPDLOG_ERROR(msg);
        throw Exception(msg);
    }

    span<filesystem::path> GetDirectories() {
        return Directories;
    }

    void Init() {
        Directories.clear();

        if (!Settings::Inferno.Descent2Path.empty())
            AddDataDirectory(Settings::Inferno.Descent2Path.parent_path());

        // Search D1 before D2 because some people might have a descent.hog in their d2 directory
        // (directories are searched in reverse order)
        if (!Settings::Inferno.Descent1Path.empty())
            AddDataDirectory(Settings::Inferno.Descent1Path.parent_path());

        if (!Settings::Inferno.Descent3Path.empty())
            AddDataDirectory(Settings::Inferno.Descent3Path);

        for (auto& path : Settings::Inferno.DataPaths)
            AddDataDirectory(path);
    }

    void AddDataDirectory(const filesystem::path& path) {
        if (!filesystem::exists(path)) {
            SPDLOG_WARN("Tried to add invalid path: {}", path.string());
            return;
        }

        SPDLOG_INFO("Adding data directory {}", path.string());
        Directories.push_back(path);
    }

    Option<filesystem::path> TryFindFile(const filesystem::path& file) {
        if (filesystem::exists(file)) // check current directory or absolute path first
            return file;

        // reverse so last directories are searched first
        for (auto& dir : Directories | std::views::reverse) {
            // D1 can override the default D2 resources by placing them in a "d1" folder
            if (Game::Level.IsDescent1()) {
                auto d1Path = dir / "d1" / file;
                if (filesystem::exists(d1Path))
                    return d1Path;
            }

            auto path = dir / file;
            if (filesystem::exists(path))
                return path;

            path = dir / "missions" / file; // for vertigo
            if (filesystem::exists(path))
                return path;
        }

        return {};
    }

    void MountZip(const std::filesystem::path& path, string_view levelName = "") {
        auto zip = ZipFile::Open(path);

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

    void MountDirectory(const std::filesystem::path& path, bool includeSpecialFolders = true, string_view extFilter = {}) {
        if (!filesystem::exists(path) || !filesystem::is_directory(path)) return;

        SPDLOG_INFO("Mounting directory: {}", path.string());

        for (auto& entry : filesystem::directory_iterator(path)) {
            if (entry.is_directory()) {
                // mount files in special directories
                auto folder = entry.path().filename().string(); // filename returns the folder 

                if (includeSpecialFolders) {
                    if (String::InvariantEquals(folder, "models") ||
                        String::InvariantEquals(folder, "textures") ||
                        String::InvariantEquals(folder, "sounds") ||
                        String::InvariantEquals(folder, "music")) {
                        MountDirectory(entry.path(), false, extFilter);
                    }
                }
            }
            else {
                auto ext = entry.path().extension().string();
                if (!extFilter.empty() && !String::InvariantEquals(ext, extFilter)) continue;

                if (String::InvariantEquals(ext, ".hog") ||
                    String::InvariantEquals(ext, ".dxa") ||
                    String::InvariantEquals(ext, ".zip") ||
                    String::InvariantEquals(ext, ".bak") ||
                    String::InvariantEquals(ext, ".sav"))
                    continue; // don't index archives or level backup files

                auto key = String::ToLower(entry.path().filename().string());

                if (Assets.contains(key)) {
                    SPDLOG_INFO("Updating {} to {}", key, entry.path().string());
                }

                Assets[key] = ResourceHandle::FromFilesystem(entry.path());
            }
        }
    }

    void MountModZip(const Level& level, const std::filesystem::path& path) {
        auto zip = ZipFile::Open(path);

        if (!zip) {
            SPDLOG_WARN("Unable to open zip {}", path.string());
            return;
        }

        if (auto manifest = ReadModManifest(*zip)) {
            if (!manifest->SupportsLevel(level)) return;
        }
        else {
            SPDLOG_WARN("Mod {} is missing manifest.yml", path.string());
            return;
        }

        SPDLOG_INFO("Mounting mod: {}", path.string());

        for (auto& entry : zip->GetEntries()) {
            //auto key = fmt::format("{}:{}", path.filename().string(), entry);
            auto key = String::ToLower(entry);
            Assets[key] = ResourceHandle::FromZip(path, entry);
        }
    }

    void MountModDirectory(const Level& level, const std::filesystem::path& path) {
        auto manifestPath = path / MOD_MANIFEST_FILE;

        if (!filesystem::exists(manifestPath)) {
            SPDLOG_WARN("Mod {} is missing manifest.yml", path.string());
            return;
        }

        auto text = File::ReadAllText(manifestPath);
        auto manifest = ReadModManifest(text);
        if (!manifest.SupportsLevel(level))
            return;

        MountDirectory(path);
    }

    // Mounts the contents of a hog, zip, or dxa
    bool MountArchive(const std::filesystem::path& path) {
        auto ext = path.extension().string();

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
            MountZip(path);
            return true;
        }

        return false;
    }

    // Mounts dxas, zips, and hogs in the directory
    void MountArchives(const std::filesystem::path& path, string_view extFilter = {}) {
        SPDLOG_INFO("Mounting archives in directory: {}", path.string());

        for (auto& entry : filesystem::directory_iterator(path)) {
            if (!entry.is_directory()) {
                auto ext = entry.path().extension().string();
                if (!extFilter.empty() && !String::InvariantEquals(ext, extFilter)) continue;

                if (String::InvariantEquals(ext, ".dxa") ||
                    String::InvariantEquals(ext, ".zip") ||
                    String::InvariantEquals(ext, ".hog")) {
                    MountArchive(entry);
                }
            }
        }
    }

    void FileSystem::Mount(const std::filesystem::path& path) {
        if (path.empty()) return;

        if (is_directory(path)) {
            MountDirectory(path, true);
        }
        else {
            MountArchive(path);
        }
    }

    Option<Image> ReadImage(const string& name, bool srgb) {
        auto ext = String::ToLower(String::Extension(name));
        Image image;

        if (ext.empty()) {
            // prioritize dds
            if (auto dds = FileSystem::ReadAsset(name + ".dds")) {
                image.LoadDDS(*dds, srgb);
            }
            else if (auto png = FileSystem::ReadAsset(name + ".png")) {
                image.LoadWIC(*png, srgb);
            }
            else if (auto tga = FileSystem::ReadAsset(name + ".tga")) {
                image.LoadTGA(*tga, srgb);
            }
        }
        else if (auto data = FileSystem::ReadAsset(name)) {
            if (ext == ".dds") {
                image.LoadDDS(*data, srgb);
            }
            else if (ext == ".png") {
                image.LoadWIC(*data, srgb);
            }
            else if (ext == ".tga") {
                image.LoadTGA(*data, srgb);
            }
        }

        if (!image.GetPixels()) return {};
        return image;
    }

    void Unmount() {
        Assets.clear();
    }

    void MountMainMenu() {
        MountArchives("d1/", ".dxa");
        MountDirectory("assets", true);

        //for (auto& [key, value] : Assets)
        //    SPDLOG_INFO("{} - {}", key, value.path.string());
    }

    void MountLevel(const Level& level, const filesystem::path& missionPath) {
        Assets.clear();

        if (level.IsDescent1()) {
            MountArchives("d1/", ".dxa");
            MountArchive("d1/descent.hog");
            MountDirectory("assets/", true);
            MountDirectory("d1/", true);
        }
        else {
            MountArchives("d2/", ".dxa");
            MountArchive("d2/descent2.hog");
            MountDirectory("assets/", true);
            MountDirectory("d2/", true);
        }

        if (Settings::Inferno.Descent3Enhanced)
            MountDirectory(Settings::Inferno.Descent3Path);

        for (auto& mod : ReadModOrder(MOD_INDEX_FILE)) {
            auto zipPath = MOD_FOLDER / mod;
            zipPath.replace_extension(".zip");
            auto path = MOD_FOLDER / mod;

            // Prioritize the unpacked directory
            if (filesystem::exists(path))
                MountModDirectory(level, path);
            else if (filesystem::exists(zipPath))
                MountModZip(level, zipPath);
        }

        if (missionPath.empty()) {
            // Mount the level folder (loose mission)
            filesystem::path levelPath = level.Path;
            levelPath.replace_extension("");

            if (filesystem::exists(levelPath))
                MountDirectory(levelPath, true);
        }
        else {
            Mount(missionPath);

            // Mount the mission addon zip [path/mission.zip]
            filesystem::path addon = missionPath;
            addon.replace_extension(".zip");
            if (filesystem::exists(addon))
                MountZip(addon, level.FileName);

            // Mount the mission addon folder [path/mission]
            addon.replace_extension("");
            if (filesystem::exists(addon) && filesystem::is_directory(addon))
                MountDirectory(addon, true);

            // Mount the level subfolder. [path/mission/level]
            filesystem::path levelFolder = addon / level.FileName;
            levelFolder.replace_extension("");

            if (filesystem::exists(levelFolder))
                MountDirectory(levelFolder, true);
        }

        //for (auto& [key, value] : Assets)
        //    SPDLOG_INFO("{} - {}", key, value.path.string());
    }

    Option<List<ubyte>> ReadAsset(string name) {
        name = String::ToLower(name);

        if (auto resource = Assets.find(name); resource != Assets.end()) {
            auto& asset = resource->second;
            switch (asset.source) {
                case Filesystem:
                    return File::ReadAllBytes(asset.path);
                case Hog: {
                    HogReader hog(asset.path);
                    return hog.TryReadEntry(name);
                }
                case Zip: {
                    if (auto zip = File::OpenZip(asset.path)) {
                        return zip->TryReadEntry(asset.name);
                    }
                    else {
                        SPDLOG_ERROR("Unable to read {} from {}", name, asset.path.string());
                    }
                    break;
                }
                default: {
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

    Option<ResourceHandle> FindAsset(string name) {
        name = String::ToLower(name);
        auto resource = Assets.find(name);
        return resource != Assets.end() ? Option(resource->second) : std::nullopt;
    }
}
