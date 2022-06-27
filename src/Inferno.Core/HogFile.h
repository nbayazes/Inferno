#pragma once
#include <variant>
#include "Types.h"
#include "Utility.h"

namespace Inferno {
    struct HogEntry {
        string Name;
        size_t Offset{};
        size_t Size{};
        filesystem::path Path; // Filesystem path for imported files
        Option<int> Index; // HOG index for saved files

        string NameWithoutExtension() const {
            auto i = Name.find('.');
            if (i == string::npos) return "";
            return Name.substr(0, i);
        }

        // Extension including the dot
        string Extension() const {
            auto i = Name.find('.');
            if (i == string::npos) return "";
            return Name.substr(i);
        }

        // Indicates if the item is a new file being imported
        bool IsImport() const { return !Path.empty(); }

        bool IsLevel() const {
            auto ext = Extension();
            return String::InvariantEquals(ext, ".rl2") || String::InvariantEquals(ext, ".rdl");
        }
    };

    // Contains menu backgrounds, palettes, music, levels
    // A hog file is simply a list of files joined together with name and length headers.
    class HogFile {
    public:
        List<HogEntry> Entries;
        std::filesystem::path Path;

        List<ubyte> ReadEntry(const HogEntry& entry) const;

        List<ubyte> ReadEntry(string_view name) const {
            return ReadEntry(FindEntry(name));
        }

        // Tries to read an entry, returns empty data if invalid.
        List<ubyte> TryReadEntry(int index) const;
        List<ubyte> TryReadEntry(string_view entry) const;

        bool Exists(string_view entry) const;
        const HogEntry& FindEntry(string_view entry) const;

        bool ContainsFileType(string_view extension) const {
            for (auto& entry : Entries) {
                if (entry.Name.ends_with(extension)) return true;
            }

            return false;
        }

        bool IsDescent1() const { return ContainsFileType("rdl"); }
        bool IsDescent2() const { return ContainsFileType("rl2"); }

        // Gets the path to the corresponding mission description file
        std::filesystem::path GetMissionPath() {
            filesystem::path path = Path;
            auto ext = IsDescent1() ? ".msn" : ".mn2";
            return path.replace_extension(ext);
        }

        HogFile() = default;
        ~HogFile() = default;
        HogFile(const HogFile&) = delete;
        HogFile(HogFile&&) = default;
        HogFile& operator=(const HogFile&) = delete;
        HogFile& operator=(HogFile&&) = default;

        // Adds or updates entry data and resaves the file
        void AddOrUpdateEntry(string_view name, span<ubyte> data);

        static HogFile Read(std::filesystem::path file);
        static constexpr int MAX_ENTRIES = 250;

        // Saves entries from the current HOG to a new file
        HogFile Save(span<HogEntry> entries, filesystem::path dest = "");

        HogFile SaveCopy(filesystem::path dest) {
            return Save(Entries, dest);
        }

        // Exports an entry to a folder
        void Export(int index, filesystem::path path);

        List<string> GetContents() {
            return Seq::map(Entries, [](const auto& e) { return e.Name; });
        }

        List<string> GetContents(string filter) const {
            auto entries = Seq::map(Entries, [](const HogEntry& e) { return e.Name; });
            return Seq::filter(entries, filter, true);
        }

        List<HogEntry> GetLevels() {
            return Seq::filter(Entries, [](const HogEntry& e) { return e.IsLevel(); });
        }

        static void CreateFromEntry(filesystem::path path, string_view name, span<ubyte> data);

        static void AppendEntry(filesystem::path path, string_view name, span<ubyte> data);
    private:

    };
}