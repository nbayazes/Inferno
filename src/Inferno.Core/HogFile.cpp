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

    filesystem::path GetTempHogName(const HogFile& hog) {
        filesystem::path tempPath = hog.Path;
        tempPath.replace_extension(".tmp");
        return tempPath;
    }

    void SwapTempFile(const HogFile& hog) {
        filesystem::path backupPath = hog.Path;
        backupPath.replace_extension(".bak");

        if (filesystem::exists(backupPath))
            filesystem::remove(backupPath);

        // Replace existing hog file with temp
        auto temp = GetTempHogName(hog);
        filesystem::rename(hog.Path, backupPath);
        filesystem::rename(temp, hog.Path);
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

    void WriteEntry(StreamWriter& writer, string_view name, span<ubyte> data) {
        if (data.empty()) return;
        writer.WriteString(string(name), 13);
        writer.Write((int32)data.size());
        writer.WriteBytes(data);
    }

    void HogFile::AddOrUpdateEntry(string_view name, span<ubyte> data) {
        // write to temp file. swap
        filesystem::path tempPath = Path;
        tempPath.replace_extension(".tmp");

        {
            std::ofstream file(tempPath, std::ios::binary);
            StreamWriter writer(file);
            writer.WriteString("DHF", 3);

            bool foundEntry = false;

            for (auto& entry : Entries) {
                if (String::InvariantEquals(entry.Name, name)) {
                    WriteEntry(writer, entry.Name, data);
                    foundEntry = true;
                }
                else {
                    // Copy existing entry data
                    auto entryData = ReadEntry(entry);
                    WriteEntry(writer, entry.Name, entryData);
                }
            }

            // Append the new entry
            if (!foundEntry)
                WriteEntry(writer, string(name), data);
        }

        // Swap the temp file for the existing
        filesystem::path backupPath = Path;
        backupPath.replace_extension(".bak");
        filesystem::remove(backupPath);
        filesystem::rename(Path, backupPath);
        filesystem::rename(tempPath, Path);

        // Reload entries from file
        *this = Read(Path);
    }

    HogFile HogFile::Read(filesystem::path file) {
        HogFile hog{};
        hog.Path = file;
        StreamReader reader(file);

        auto id = reader.ReadString(3);
        if (id != "DHF") // Descent Hog File
            throw Exception("Invalid Hog file");

        int index = 0;
        while (!reader.EndOfFile()) {
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

    HogFile HogFile::Save(span<HogEntry> entries, filesystem::path dest) {
        if (Entries.size() > MAX_ENTRIES)
            throw Exception("HOG files can only contain 250 entries");

        HogFile newFile{};
        newFile.Path = dest.empty() ? Path : dest;
        newFile.Entries.assign(entries.begin(), entries.end());

        if (newFile.Path == Path)
            dest = GetTempHogName(*this);

        {
            std::ofstream file(dest, std::ios::binary);
            StreamWriter writer(file);
            writer.WriteString("DHF", 3);

            for (auto& entry : entries) {
                auto data = ReadEntry(entry);
                WriteEntry(writer, entry.Name, data);
            }
        }

        if (newFile.Path == Path)
            SwapTempFile(*this);

        return newFile;
    }

    void HogFile::CreateFromEntry(filesystem::path path, string_view name, span<ubyte> data) {
        std::ofstream file(path, std::ios::binary);
        StreamWriter writer(file);
        writer.WriteString("DHF", 3);

        WriteEntry(writer, name, data);
    }

    void HogFile::AppendEntry(filesystem::path path, string_view name, span<ubyte> data) {
        std::ofstream file(path, std::ios::binary | std::ios::app);
        StreamWriter writer(file);
        WriteEntry(writer, name, data);
    }

    void HogFile::Export(int index, filesystem::path path) {
        auto data = TryReadEntry(index);
        if (data.empty()) throw Exception("File does not exist");

        std::ofstream file(path, std::ios::binary);
        file.write((char*)data.data(), data.size());
    }
}