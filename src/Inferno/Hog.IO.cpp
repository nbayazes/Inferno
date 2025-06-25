#include "pch.h"
#include "Hog.IO.h"

namespace Inferno {
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

    void CopySwap(const filesystem::path& dest, const filesystem::path& source) {
        // create a backup
        if (filesystem::exists(dest)) {
            filesystem::path backupPath = dest;
            backupPath.replace_extension(".bak");
            filesystem::copy(dest, backupPath, filesystem::copy_options::overwrite_existing);
        }

        // move new file to destination
        filesystem::remove(dest); // Remove existing
        filesystem::rename(source, dest); // Rename source to destination
    }

    void HogWriter::AddOrUpdate(const filesystem::path& path, string_view name, span<ubyte> data) try {
        filesystem::path temp = path;
        temp.replace_extension(".tmp");

        {
            HogWriter writer(temp);
            bool existing = false;

            // Copy existing entries
            if (filesystem::exists(path)) {
                HogReader reader(path);

                for (auto& entry : reader.Entries()) {
                    if (String::InvariantEquals(entry.Name, name)) {
                        SPDLOG_INFO("Replacing existing entry {}", entry.Name);
                        writer.WriteEntry(entry.Name, data);
                        existing = true;
                    }
                    else {
                        SPDLOG_INFO("Writing entry {}", entry.Name);
                        auto entryData = reader.ReadEntry(entry.Name);
                        writer.WriteEntry(entry.Name, entryData);
                    }
                }
            }

            // Write new entry
            if (!existing) {
                SPDLOG_INFO("Writing new {}", name);
                writer.WriteEntry(name, data);
            }
        } // HogWriter

        CopySwap(path, temp);
    }
    catch (const std::exception& e) {
        //ShowErrorMessage(e);
        SPDLOG_ERROR(e.what());
    }

    bool HogWriter::Remove(const filesystem::path& path, string_view name) try {
        filesystem::path temp = path;
        temp.replace_extension(".tmp");
        bool found = false;

        // Copy existing entries
        if (filesystem::exists(path)) {
            HogWriter writer(temp);
            HogReader reader(path);

            for (auto& entry : reader.Entries()) {
                if (String::InvariantEquals(entry.Name, name)) {
                    found = true;
                    SPDLOG_INFO("Removing entry {}", entry.Name);
                }
                else {
                    SPDLOG_INFO("Writing entry {}", entry.Name);
                    auto entryData = reader.ReadEntry(entry.Name);
                    writer.WriteEntry(entry.Name, entryData);
                }
            }
        }

        CopySwap(path, temp);
        return found;
    }
    catch (const std::exception& e) {
        //ShowErrorMessage(e);
        SPDLOG_ERROR(e.what());
        return false;
    }

    HogReader::HogReader(filesystem::path path) : _reader(path), _path(std::move(path)) {
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
