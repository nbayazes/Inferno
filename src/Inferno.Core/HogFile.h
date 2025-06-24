#pragma once
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

    List<ubyte> ReadHogEntry(StreamReader stream, const HogEntry& entry);

    // Contains menu backgrounds, palettes, music, levels
    // A hog file is simply a list of files joined together with name and length headers.
    class HogFile {
    public:
        List<HogEntry> Entries;
        filesystem::path Path;

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
        Option<HogEntry> FindEntryOfType(string_view extension) const {
            for (auto& entry : Entries) {
                if (entry.Name.ends_with(extension)) return entry;
            }

            return {};
        }

        bool IsDescent1() const { return ContainsFileType("rdl") || ContainsFileType("sdl"); }
        bool IsDescent2() const { return ContainsFileType("rl2"); }
        bool IsShareware() const { return ContainsFileType("sdl"); }

        // Returns true if the HOG is descent.hog or descent2.hog
        bool IsRetailMission() const {
            return String::InvariantEquals(Path.filename().string(), "descent.hog") || String::InvariantEquals(Path.filename().string(), "descent2.hog");
        }

        // Gets the path to the corresponding mission description file
        std::filesystem::path GetMissionPath() const {
            filesystem::path path = Path;
            auto ext = IsDescent1() ? ".msn" : ".mn2";
            return path.replace_extension(ext);
        }

        HogFile() = default;
        ~HogFile() = default;
        explicit HogFile(const HogFile&) = default;
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

    // Creates a new hog file and writes to it
    class HogWriter {
        StreamWriter _writer;
    public:
        HogWriter(const filesystem::path& path) : _writer(path) {
            _writer.WriteString("DHF", 3);
        }

        void WriteEntry(string_view name, span<ubyte> data);
    };

    // Opens a hog file for reading. Locks the file for the lifetime of the object.
    class HogReader {
        StreamReader _reader;
        List<HogEntry> _entries;
        filesystem::path _path;
        //static constexpr int MAX_ENTRIES = 250;
    public:
        HogReader(filesystem::path path);

        // Tries to read an entry from the hog
        Option<List<ubyte>> TryReadEntry(string_view name);

        // Reads an entry from the hog and throws if it is not found
        List<ubyte> ReadEntry(string_view name) {
            if(auto entry = TryReadEntry(name))
                return *entry;

            throw Exception(fmt::format("Unable to read file `{}` from `{}`", name, _path.string()));
        }

        // Tries to read an entry as ASCII text
        Option<string> TryReadEntryAsString(string_view entry) {
            auto data = TryReadEntry(entry);
            if (!data) return {};
            return string((char*)data->data(), data->size());
        }
    private:
        const Option<HogEntry> TryFindEntry(string_view entry) const {
            for (auto& e : _entries)
                if (String::InvariantEquals(e.Name, entry)) return e;

            return {};
        }
    };

    List<HogEntry> ReadHogEntries(StreamReader& reader);
}
