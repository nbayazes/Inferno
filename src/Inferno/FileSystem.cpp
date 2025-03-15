#include "pch.h"
#include <fstream>
#include <ranges>
#include "FileSystem.h"
#include "Game.h"
#include "Settings.h"
#include "Convert.h"
#include "Logging.h"

Inferno::List<Inferno::ubyte> Inferno::File::ReadAllBytes(const std::filesystem::path& path) {
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

void Inferno::File::WriteAllBytes(const std::filesystem::path& path, span<ubyte> data) {
    std::ofstream file(path, std::ios::binary);
    StreamWriter writer(file, false);
    writer.WriteBytes(data);
    SPDLOG_INFO("Wrote {} bytes to {}", data.size(), path.string());
}

std::string Inferno::File::ReadAllText(const filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        SPDLOG_WARN("Unable to open file `{}`", path.string());
        return {};
    }

    return { std::istreambuf_iterator(stream), std::istreambuf_iterator<char>() };
}

std::vector<std::string> Inferno::File::ReadLines(const filesystem::path& path) {
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
