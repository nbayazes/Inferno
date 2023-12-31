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
        auto msg = fmt::format(L"File not found: {}", path.wstring());
        throw Exception(Convert::ToString(msg).c_str());
    }

    auto size = filesystem::file_size(path);
    List<ubyte> buffer(size);
    if (!file.read((char*)buffer.data(), size)) {
        auto msg = fmt::format(L"File read error: {}", path.wstring());
        throw Exception(Convert::ToString(msg).c_str());
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

namespace Inferno::FileSystem {
    List<filesystem::path> Directories;

    filesystem::path FindFile(const filesystem::path& file) {
        if (auto path = TryFindFile(file))
            return *path;

        auto msg = fmt::format(L"File not found: {}", file.wstring());
        SPDLOG_ERROR(msg);
        throw Exception(Convert::ToString(msg).c_str());
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

        for (auto& path : Settings::Inferno.DataPaths)
            AddDataDirectory(path);
    }

    void AddDataDirectory(const filesystem::path& path) {
        if (!filesystem::exists(path)) {
            SPDLOG_WARN(L"Tried to add invalid path: {}", path.wstring());
            return;
        }

        SPDLOG_INFO(L"Adding data directory {}", path.wstring());
        Directories.push_back(path);
    }

    Option<filesystem::path> TryFindFile(const filesystem::path& file) {
        if (filesystem::exists(file)) // check current directory or absolute path first
            return wstring(file);

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
