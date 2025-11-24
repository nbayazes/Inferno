#include "pch.h"
#include "ZipFile.h"
#include <zip/zip.h>
#include "Utility.h"

namespace Inferno {
    class ZipFile final : public IZipFile {
        zip_t* _zip = nullptr;
        List<string> _entries;
        filesystem::path _path;

    public:
        static Ptr<IZipFile> Open(const filesystem::path& path) {
            auto zip = zip_open(path.string().c_str(), 0, 'r');
            if (!zip) return {};

            auto file = std::make_unique<ZipFile>();

            file->_zip = zip;
            file->_path = path;

            auto totalEntries = zip_entries_total(zip);
            List<string> fileNames;

            // index the entries
            for (int i = 0; i < totalEntries; i++) {
                if (zip_entry_openbyindex(zip, i) < 0)
                    break;

                string name = zip_entry_name(zip);
                file->_entries.push_back(name);
                zip_entry_close(zip);
            }

            return file;
        }

        span<string> GetEntries() override { return _entries; }

        bool Contains(string_view fileName) const override {
            return Seq::contains(_entries, fileName);
        }

        /*ResourceHandle Find(string_view fileName) {
            return Seq::tryItem(_entries, fileName);
        }*/

        Option<List<ubyte>> TryReadEntry(string_view fileName) const override try {
            List<byte> data;
            if (zip_entry_open(_zip, string(fileName).c_str()) == 0) {
                void* buffer;
                size_t bufferSize;
                auto readBytes = zip_entry_read(_zip, &buffer, &bufferSize);
                if (readBytes > 0) {
                    //SPDLOG_INFO("Read file from {}:{}", _path.string(), fileName);
                    data.assign((byte*)buffer, (byte*)buffer + bufferSize);
                }

                zip_entry_close(_zip);
            }

            if (data.empty())
                return {};

            return data;
        }
        catch (const std::exception& e) {
            SPDLOG_ERROR("Error reading {}: {}", _path.string(), e.what());
            return {};
        }

        ZipFile(const ZipFile&) = delete;
        ZipFile(ZipFile&&) = default;
        ZipFile& operator=(const ZipFile&) = delete;
        ZipFile& operator=(ZipFile&&) = default;

        ~ZipFile() override {
            zip_close(_zip);
        }

        ZipFile() {}

        const filesystem::path& Path() const override { return _path; }
    };

    Ptr<IZipFile> OpenZip(const filesystem::path& path) {
        return ZipFile::Open(path);
    }
}
