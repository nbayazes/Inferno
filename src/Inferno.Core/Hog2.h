#pragma once

#include "Types.h"
#include "Streams.h"

// Descent 3 HOG2 file
namespace Inferno {
    class Hog2 {
        static constexpr int PSFILENAME_LEN = 35;
        static constexpr int HOG_HDR_SIZE = 64;

        Dictionary<string, int> _lookup;
    public:
        filesystem::path Path;

        struct Entry {
            string name;
            uint flags;
            uint len;
            uint timestamp;
            int64 offset;
        };

        static Hog2 Read(const filesystem::path& path) {
            Hog2 hog;
            hog.Path = path;

            StreamReader r(path);
            auto id = r.ReadString(4);
            if (id != "HOG2")
                throw Exception("Not a HOG2 file");

            uint nfiles = r.ReadUInt32();
            long file_data_offset = r.ReadUInt32();

            hog.Entries.reserve(nfiles);

            r.Seek(4 + HOG_HDR_SIZE);
            long offset = file_data_offset;
            for (uint i = 0; i < nfiles; i++) {
                auto& entry = hog.Entries.emplace_back();
                entry.name = String::ToLower(r.ReadString(PSFILENAME_LEN + 1));
                entry.flags = r.ReadUInt32();
                entry.len = r.ReadUInt32();
                entry.timestamp = r.ReadUInt32();
                entry.offset = offset;
                offset += entry.len;

                hog._lookup.insert({ entry.name, i });
            }

            return hog;
        }

        List<Entry> Entries;

        List<ubyte> ReadEntry(int index) {
            if (!Seq::inRange(Entries, index))
                throw Exception("Invalid entry index");

            StreamReader r(Path);
            const auto& entry = Entries[index];
            r.Seek(entry.offset);
            List<ubyte> data(entry.len);
            r.ReadBytes(data);
            return data;
        }

        Option<List<ubyte>> ReadEntry(string name) {
            name = String::ToLower(name);
            if (!_lookup.contains(name))
                return {};
            
            return ReadEntry(_lookup[name]);
        }
    };
}