#pragma once
#include <fstream>
#include "Streams.h"
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
            return String::NameWithoutExtension(Name);
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
            return String::InvariantEquals(ext, ".rl2") || String::InvariantEquals(ext, ".rdl") || String::InvariantEquals(ext, ".sdl");
        }

        bool IsBriefing() const {
            return String::InvariantEquals(Extension(), ".txb");
        }

        bool IsHam() const {
            return String::InvariantEquals(Extension(), ".ham");
        }
    };

    // Contains menu backgrounds, palettes, music, levels
    // A hog file is simply a list of files joined together with name and length headers.
    class HogFile {
    public:
        List<HogEntry> Entries;
        filesystem::path Path;

        // Reads data from an entry. Can come from the HogFile Path or a file system path.
        List<ubyte> ReadEntry(const HogEntry& entry) const;

        List<ubyte> ReadEntry(string_view name) const {
            return ReadEntry(FindEntry(name));
        }

        // Tries to read an entry, returns empty data if invalid.
        List<ubyte> TryReadEntry(int index) const;
        List<ubyte> TryReadEntry(string_view entry) const;

        // Returns an empty string if entry is not found
        string TryReadEntryAsString(string_view entry) const {
            auto data = TryReadEntry(entry);
            if (data.empty()) return {};
            return string((char*)data.data(), data.size());
        }

        bool Exists(string_view entry) const;
        const HogEntry& FindEntry(string_view entry) const;
        const HogEntry* TryFindEntry(string_view entry) const;

        bool ContainsFileType(string_view extension) const {
            for (auto& entry : Entries) {
                if (entry.Name.ends_with(extension)) return true;
            }

            return false;
        }

        // Returns the first file with the provided extension
        Option<HogEntry> FindEntryOfType(string_view extension) {
            for (auto& entry : Entries) {
                if (entry.Name.ends_with(extension)) return entry;
            }

            return {};
        }

        bool IsDescent1() const { return ContainsFileType("rdl"); }
        bool IsDescent2() const { return ContainsFileType("rl2"); }

        // Gets the path to the corresponding mission description file
        std::filesystem::path GetMissionPath() const {
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

        static HogFile Read(const std::filesystem::path& file);
        static constexpr int MAX_ENTRIES = 250;

        List<string> GetContents() const {
            return Seq::map(Entries, [](const auto& e) { return e.Name; });
        }

        List<string> GetContents(const string& filter) const {
            auto entries = Seq::map(Entries, [](const HogEntry& e) { return e.Name; });
            return Seq::filter(entries, filter, true);
        }

        List<HogEntry> GetLevels() const {
            return Seq::filter(Entries, [](const HogEntry& e) { return e.IsLevel(); });
        }
    };

    class HogWriter {
        std::ofstream _stream;
        StreamWriter _writer;
        int _entries = 0;
        //static constexpr int MAX_ENTRIES = 250;
    public:
        HogWriter(const filesystem::path& path) : _stream(path, std::ios::binary), _writer(_stream) {
            _writer.WriteString("DHF", 3);
        }

        void WriteEntry(string_view name, span<ubyte> data) {
            if (data.empty()) return;
            //if (_entries >= MAX_ENTRIES) throw Exception("Cannot have more than 250 entries!");
            _writer.WriteString(string(name), 13);
            _writer.Write((int32)data.size());
            _writer.WriteBytes(data);
            _entries++;
        }
    };
}