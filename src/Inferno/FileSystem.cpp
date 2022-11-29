#include "pch.h"
#include <fstream>
#include <ranges>
#include "FileSystem.h"
#include "Game.h"
#include "Settings.h"
#include "Convert.h"

namespace Inferno::FileSystem {
    List<filesystem::path> Directories;

    wstring FindFile(filesystem::path file) {
        if (auto path = TryFindFile(file))
            return path.value();

        auto msg = fmt::format(L"File not found: {}", file.wstring());
        SPDLOG_ERROR(msg);
        throw Exception(Convert::ToString(msg).c_str());
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

    void AddDataDirectory(filesystem::path path) {
        if (!filesystem::exists(path)) {
            SPDLOG_WARN(L"Tried to add invalid path: {}", path.wstring());
            return;
        }

        SPDLOG_INFO(L"Adding data directory {}", path.wstring());
        Directories.push_back(path);
    }

    Option<filesystem::path> TryFindFile(filesystem::path file) {
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

    List<char> ReadFileBytes(std::filesystem::path path) {
        if (std::filesystem::exists(path)) return {};
        std::ifstream stream(path, std::ios::binary);
        return { std::istreambuf_iterator(stream), std::istreambuf_iterator<char>() };
    }
}

