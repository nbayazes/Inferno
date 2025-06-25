#pragma once
#include "HogFile.h"
#include "Streams.h"

namespace Inferno {

    // Creates a new hog file and writes to it
    class HogWriter {
        StreamWriter _writer;
    public:
        HogWriter(const filesystem::path& path) : _writer(path) {
            _writer.WriteString("DHF", 3);
        }

        void WriteEntry(string_view name, span<ubyte> data);

        static filesystem::path GetTemporaryPath(const HogFile& hog) {
            filesystem::path tempPath = hog.Path;
            tempPath.replace_extension(".tmp");
            return tempPath;
        }

        // Adds or updates a single file in a hog. Creates the hog if it doesn't exist.
        static void AddOrUpdate(const filesystem::path& path, string_view name, span<ubyte> data);

        // Removes a single file in a hog. Returns true if the file existed.
        static bool Remove(const filesystem::path& path, string_view name);
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
            if (auto entry = TryReadEntry(name))
                return *entry;

            throw Exception(fmt::format("Unable to read file `{}` from `{}`", name, _path.string()));
        }

        // Tries to read an entry as ASCII text
        Option<string> TryReadEntryAsString(string_view entry) {
            auto data = TryReadEntry(entry);
            if (!data) return {};
            return string((char*)data->data(), data->size());
        }

        span<const HogEntry> Entries() const { return _entries; }

        const Option<HogEntry> TryFindEntry(string_view entry) const {
            for (auto& e : _entries)
                if (String::InvariantEquals(e.Name, entry)) return e;

            return {};
        }
    };

    List<HogEntry> ReadHogEntries(StreamReader& reader);
}
