#include "pch.h"
#include "HogFile.h"
#include <spdlog/spdlog.h>
#include "Pig.h"

namespace Inferno {
    List<ubyte> ReadFileToMemory(const wstring& file, size_t offset, size_t length) {
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

    bool HogFile::Exists(string_view entry) const {
        for (auto& e : Entries)
            if (String::InvariantEquals(e.Name, entry)) return true;

        return false;
    }

    const HogEntry& HogFile::FindEntry(string_view entry) const {
        for (auto& e : Entries)
            if (String::InvariantEquals(e.Name, entry)) return e;

        throw Exception(fmt::format("{} not found in hog file", entry));
    }

    const HogEntry* HogFile::TryFindEntry(string_view entry) const {
        for (auto& e : Entries)
            if (String::InvariantEquals(e.Name, entry)) return &e;

        return nullptr;
    }

    List<ubyte> ReadHogEntry(StreamReader stream, const HogEntry& entry) {
        List<ubyte> data(entry.Size);
        stream.Seek(entry.Offset);
        stream.ReadBytes(data);
        return data;
    }

    List<HogEntry> ReadHogEntries(StreamReader& reader) {
        List<HogEntry> entries;

        auto id = reader.ReadString(3);
        if (id != "DHF") // Descent Hog File
            throw Exception("Invalid Hog file");

        int index = 0;
        while (!reader.EndOfStream()) {
            HogEntry entry;
            entry.Name = reader.ReadString(13);
            if (entry.Name == "") break;
            entry.Size = reader.ReadInt32();
            //if (entry.Size == 0) throw Exception("Hog entry has size of zero");
            entry.Offset = reader.Position();
            entry.Index = index++;
            entries.push_back(entry);
            reader.SeekForward(entry.Size);
        }

        return entries;
    }

    HogFile HogFile::Read(const filesystem::path& file) {
        HogFile hog{};
        hog.Path = file;
        StreamReader reader(file);
        hog.Entries = ReadHogEntries(reader);
        return hog;
    }

    void HogWriter::WriteEntry(string_view name, span<ubyte> data) {
        if (data.empty()) return;
        // the original game seems to indicate an entry limit of 250, but it's unclear if this is enforced
        //if (_entries >= MAX_ENTRIES) throw Exception("Cannot have more than 250 entries!");
        //if (_writer.Path().empty())
        //    SPDLOG_INFO("Writing hog entry: {}", name);
        //else
        //    SPDLOG_INFO("Writing {}:{}", _writer.Path().string(), name);

        _writer.WriteString(string(name), 13);
        _writer.Write((int32)data.size());
        _writer.WriteBytes(data);
        //_entries++;
    }

    HogReader::HogReader(filesystem::path path): _reader(path), _path(std::move(path)) {
        _entries = ReadHogEntries(_reader);
    }

    Option<List<ubyte>> HogReader::TryReadEntry(string_view name) {
        if (auto entry = TryFindEntry(name)) {
            List<ubyte> data(entry->Size);
            _reader.Seek(entry->Offset);
            _reader.ReadBytes(data);
            return data;
        }

        return {};
    }
}
