#include "pch.h"
#include <fstream>
#include <ranges>
#include "FileSystem.h"
#include "Game.h"
#include "Settings.h"
#include "Logging.h"
#include <zip/zip.h>

namespace Inferno {
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
            auto msg = fmt::format("File not found: {}", path.string());
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
}

namespace Inferno::FileSystem {
    List<filesystem::path> Directories;

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

        AddDataDirectory("./data");
        AddDataDirectory("./textures");

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
}
