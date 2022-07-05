#pragma once

#include "Types.h"
#include "Streams.h"

// Descent 3 HOG2 file
namespace Inferno {
    struct Hog2Entry {
        string name;
        uint flags;
        uint len;
        uint timestamp;
        int64 offset;
    };

    class Hog2 {
        static constexpr int PSFILENAME_LEN = 35;
        static constexpr int HOG_HDR_SIZE = 64;

        StreamReader _reader;
        Dictionary<string, int> _lookup;

    public:

        struct Entry {
            string name;
            uint flags;
            uint len;
            uint timestamp;
            int64 offset;
        };

        Hog2(std::filesystem::path path) : _reader(path) {
            auto id = _reader.ReadString(4);
            if (id != "HOG2")
                throw Exception("Not a HOG2 file");

            uint nfiles = _reader.ReadUInt32();
            long file_data_offset = _reader.ReadUInt32();

            Entries.reserve(nfiles);

            //var names = new Dictionary<string, int>();
            _reader.Seek(4 + HOG_HDR_SIZE);
            //r.BaseStream.Position = 4 + HOG_HDR_SIZE;
            long offset = file_data_offset;
            for (uint i = 0; i < nfiles; i++) {
                auto& entry = Entries.emplace_back();
                entry.name = String::ToLower(_reader.ReadString(PSFILENAME_LEN + 1));
                entry.flags = _reader.ReadUInt32();
                entry.len = _reader.ReadUInt32();
                entry.timestamp = _reader.ReadUInt32();
                entry.offset = offset;
                offset += entry.len;

                _lookup.insert({ entry.name, i });
            }
        }

        List<Entry> Entries;

        List<ubyte> ReadEntry(int index) {
            if (!Seq::inRange(Entries, index))
                throw Exception("Invalid entry index");

            const auto& entry = Entries[index];
            _reader.Seek(entry.offset);
            List<ubyte> data(entry.len);
            _reader.ReadBytes(data);
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