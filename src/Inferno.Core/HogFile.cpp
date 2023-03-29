#include "HogFile.h"
#include "pch.h"
#include "IO.h"
#include "HogFile.h"
#include "Pig.h"
#include <fstream>
#include "Streams.h"
#include "Utility.h"

namespace Inferno {
    List<ubyte> ReadFileToMemory(wstring file, size_t offset, size_t length) {
        if (offset == 0)
            throw Exception("Hog entry offset cannot be 0");

        std::ifstream stream(file, std::ifstream::binary);

        if (length == 0) {
            stream.seekg(0, stream.end);
            length = stream.tellg();
        }

        List<ubyte> buffer;
        buffer.resize(length);
        stream.seekg(offset, stream.beg);
        stream.read((char*)buffer.data(), length);
        return buffer;
    }

    List<ubyte> HogFile::ReadEntry(const HogEntry& entry) const {
        if (entry.Path != "") {
            auto size = filesystem::file_size(entry.Path);
            if (size == 0) return {};

            std::ifstream src(entry.Path, std::ios::binary);
            List<ubyte> data(size);
            src.read((char*)data.data(), size);
            return data;
        }
        else {
            return ReadFileToMemory(Path, entry.Offset, entry.Size);
        }
    }

    List<ubyte> HogFile::TryReadEntry(int index) const {
        if (auto entry = Seq::tryItem(Entries, index))
            return ReadFileToMemory(Path, entry->Offset, entry->Size);
        else
            return {};
    }

    List<ubyte> HogFile::TryReadEntry(string_view entry) const {
        for (auto& e : Entries)
            if (String::InvariantEquals(e.Name, entry)) 
                return ReadFileToMemory(Path, e.Offset, e.Size);

        return {};
    }

    bool HogFile::Exists(string_view entry) const {
        for (auto& e : Entries)
            if (String::InvariantEquals(e.Name, entry)) return true;

        return false;
    }

    const HogEntry& HogFile::FindEntry(string_view entry) const {
        for (auto& e : Entries)
            if (String::InvariantEquals(e.Name, entry)) return e;

        throw Exception("File not found in hog file");
    }

    HogFile HogFile::Read(filesystem::path file) {
        HogFile hog{};
        hog.Path = file;
        StreamReader reader(file);

        auto id = reader.ReadString(3);
        if (id != "DHF") // Descent Hog File
            throw Exception("Invalid Hog file");

        int index = 0;
        while (!reader.EndOfStream()) {
            HogEntry entry;
            entry.Name = reader.ReadString(13);
            if (entry.Name == "") break;
            entry.Size = reader.ReadInt32();
            if (entry.Size == 0) throw Exception("Hog entry has size of zero");
            entry.Offset = reader.Position();
            entry.Index = index++;
            hog.Entries.push_back(entry);
            reader.SeekForward(entry.Size);
        }

        return hog;
    }







}