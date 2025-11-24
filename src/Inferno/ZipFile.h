#pragma once

namespace Inferno {
    struct IZipFile {
        virtual span<string> GetEntries() = 0;
        virtual Option<List<ubyte>> TryReadEntry(string_view entryName) const = 0;
        virtual const filesystem::path& Path() const = 0;

        // Returns true if the zip contains the entry
        virtual bool Contains(string_view entryName) const = 0;
        virtual ~IZipFile() = default;

        IZipFile() = default;
        IZipFile(const IZipFile&) = delete;
        IZipFile(IZipFile&&) = default;
        virtual IZipFile& operator=(const IZipFile&) = delete;
        IZipFile& operator=(IZipFile&&) = default;
    };

    // Tries to open a zip file at the given path
    Ptr<IZipFile> OpenZip(const filesystem::path& path);
}