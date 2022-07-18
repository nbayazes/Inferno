#pragma once
#include <fstream>
#include <filesystem>
#include "Types.h"
#include "Utility.h"

namespace Inferno {
    class MemoryBuffer : public std::streambuf {
    public:
        MemoryBuffer(char* p, size_t size) {
            setp(p, p + size);
            setg(p, p, p + size);
        }

    protected:
        pos_type seekoff(off_type off,
                         std::ios_base::seekdir dir,
                         std::ios_base::openmode) override {
            if (dir == std::ios_base::cur)
                gbump((int)off);
            else if (dir == std::ios_base::end)
                setg(eback(), egptr() + off, egptr());
            else if (dir == std::ios_base::beg)
                setg(eback(), eback() + off, egptr());
            return gptr() - eback();
        }

        pos_type seekpos(pos_type sp, std::ios_base::openmode which) override {
            return seekoff(sp - pos_type(off_type(0)), std::ios_base::beg, which);
        }
    };

    class MemoryStream : public std::iostream {
        MemoryBuffer _buffer;
    public:
        MemoryStream(char* p, size_t size) : std::iostream(&_buffer), _buffer(p, size) {
            rdbuf(&_buffer);
        }
    };

    // Encapsulates reading binary fixed point data from a stream.
    class StreamReader {
        std::unique_ptr<std::istream> _stream;
        std::filesystem::path _file;
        List<ubyte> _data;

        template<class T>
        T Read() {
            T b{};
            _stream->read((char*)&b, sizeof(T));
            return b;
        }
    public:
        StreamReader(span<ubyte> data, const string& name = "") {
            _stream = std::make_unique<MemoryStream>((char*)data.data(), data.size());
            _file = name;
        }

        // Takes ownership of data
        StreamReader(List<ubyte>&& data, const string& name = "") {
            _data = std::move(data);
            _stream = std::make_unique<MemoryStream>((char*)_data.data(), _data.size());
            _file = name;
        }

        StreamReader(std::unique_ptr<std::ifstream> stream) {
            _stream = std::move(stream);
        }

        StreamReader(std::filesystem::path file) {
            if (!std::filesystem::exists(file)) throw Exception("File not found");
            _stream = std::make_unique<std::ifstream>(file, std::ios::binary);
            _file = file;
        }

        StreamReader(const StreamReader&) = delete;
        StreamReader& operator=(const StreamReader&) = delete;

        StreamReader(StreamReader&& other) noexcept {
            _data = std::move(other._data);
            _stream = std::move(other._stream);
            _file.swap(other._file);
        }

        StreamReader& operator=(StreamReader&& other) noexcept {
            _data = std::move(other._data);
            _stream = std::move(other._stream);
            _file.swap(other._file);
        }

        ~StreamReader() = default;

        List<sbyte> ReadSBytes(size_t length) {
            List<sbyte> b(length);
            _stream->read(b.data(), sizeof(sbyte) * length);
            return b;
        }

        List<ubyte> ReadUBytes(size_t length) {
            List<ubyte> b(length);
            _stream->read((char*)b.data(), sizeof(ubyte) * length);
            return b;
        }

        void ReadBytes(void* buffer, size_t length) {
            _stream->read((char*)buffer, length);
        }

        void ReadBytes(span<ubyte> buffer) {
            _stream->read((char*)buffer.data(), buffer.size());
        }

        // Reads a fixed length string
        string ReadString(size_t length) {
            List<char> b(length + 1);
            _stream->read(b.data(), sizeof(char) * length);
            return { b.data() };
        }

        // Reads a null or newline terminated string up to the max length
        string ReadCString(size_t maxLen) {
            List<char> b(maxLen + 1);
            for (int i = 0; i < maxLen; i++) {
                _stream->read(&b[i], sizeof(char));
                if (b[i] == '\n') b[i] = '\0';
                if (b[i] == '\0') break;
            }
            return { b.data() };
        }

        // Reads a newline terminated string up to the max length
        string ReadStringToNewline(size_t maxLen) {
            std::vector<char> chars;
            for (int i = 0; i < maxLen; i++) {
                char c = (char)ReadByte();
                if (c == '\n') {
                    chars.push_back('\0');
                    break;
                }
                chars.push_back(c);
            }

            return { chars.data() };
        }

        ubyte ReadByte() { return Read<ubyte>(); }
        int16 ReadInt16() { return Read<int16>(); }
        uint16 ReadUInt16() { return Read<uint16>(); }
        uint32 ReadUInt32() { return Read<uint32>(); }
        int32 ReadInt32() { return Read<int32>(); }
        int64 ReadInt64() { return Read<int64>(); }
        float ReadFloat() { return Read<float>(); }

        // Reads a int32 fixed value into a float
        float ReadFix() { return FixToFloat(Read<int32>()); }


        // Reads an int32 and limits between positive values and maximum. Used to prevent allocating huge vectors due to a programming error.
        int32 ReadInt32Checked(int maximum, const char* message) {
            auto len = ReadInt32();
            if (len < 0 || len > maximum)
                throw Exception(message);
            return len;
        };

        // Reads an int32 and limits between positive values and maximum. Used to prevent allocating huge vectors due to a programming error.
        int32 ReadElementCount(int maximum = 10000) {
            return ReadInt32Checked(maximum, "Element count is out of range. This is likely a programming error but could be a corrupted file");
        };

        // Reads a 12 byte fixed point vector into a floating point vector
        Vector3 ReadVector() {
            Vector3 v;
            v.x = ReadFix();
            v.y = ReadFix();
            v.z = ReadFix();
            return v;
        }

        // Reads a floating point vector
        Vector3 ReadVector3() {
            Vector3 v;
            v.x = ReadFloat();
            v.y = ReadFloat();
            v.z = ReadFloat();
            return v;
        }

        Matrix3x3 ReadRotation() {
            auto rvec = ReadVector();
            auto uvec = ReadVector();
            auto fvec = ReadVector();
            return Matrix3x3(rvec, uvec, -fvec); // flip Z due to LH data
        }

        // Reads a 2 byte fixed angle
        float ReadFixAng() {
            return FixToFloat(ReadInt16());
        }

        // Reads a 6 byte fixed point angle vector
        Vector3 ReadAngleVec() {
            auto p = ReadFixAng();
            auto h = ReadFixAng();
            auto b = ReadFixAng();
            return Vector3(p, h, b);
        }

        // Reads a 6 byte RGB color
        Color ReadRGB() {
            auto r = ReadByte();
            auto g = ReadByte();
            auto b = ReadByte();
            return Color(r / 255.0f, g / 255.0f, b / 255.0f);
        }

        bool EndOfStream() { 
            _stream->peek(); // need to peek to ensure EOF is correct
            return _stream->eof(); 
        }

        // Current stream offset
        size_t Position() { return _stream->tellg(); }

        // Seek from the beginning
        void Seek(size_t offset) {
            _stream->seekg(offset, std::ios_base::beg);
        }

        // Seek forward from the current position
        void SeekForward(size_t offset) {
            _stream->seekg(offset, std::ios_base::cur);
        }
    };

    // Specialized stream writer for Descent binary files
    class StreamWriter {
        std::ostream& _stream;
        std::streampos _start;

    public:
        // Creates a stream writer over an output stream.
        // If relative is true, positions and seeking will be relative to when the writer
        // is created, and not the absolute beginning.
        StreamWriter(std::ostream& stream, bool relative = false) : _stream(stream) {
            _start = relative ? stream.tellp() : std::streampos(0);
        }

        template<class T>
        void Write(const T value) {
            static_assert(!std::is_floating_point<T>()); // serializing a float is always wrong for Descent files
            static_assert(!std::is_same<T, Vector3>::value); // ambiguous
            _stream.write((char*)&value, sizeof(T));
        }

        void WriteFix(float f) {
            Write(FloatToFix(f));
        }

        void WriteVector(const Vector3& v) {
            WriteFix(v.x);
            WriteFix(v.y);
            WriteFix(v.z);
        };

        void WriteRotation(const Matrix3x3& m) {
            WriteVector(m.Right());
            WriteVector(m.Up());
            WriteVector(m.Forward()); // Strangely do not have to convert from RH back to LH
        }

        // Writes an angle as 2 bytes fixed point. Take care to not exceed the range.
        void WriteAngle(float angle) {
            Write((int16)FloatToFix(angle));
        }

        // Writes an angle vector to 6 bytes fixed point
        void WriteAngles(const Vector3& angles) {
            WriteAngle(angles.x);
            WriteAngle(angles.y);
            WriteAngle(angles.z);
        }

        void WriteBytes(span<ubyte> data) {
            _stream.write((char*)data.data(), data.size());
        }

        void WriteNewlineTerminatedString(string s, size_t maxLen) {
            assert(maxLen > 0);
            if (s.size() > maxLen - 1)
                s = s.substr(0, maxLen - 1);

            s += '\n';

            _stream.write(s.data(), s.size());
        }

        // Writes a null terminated string
        void WriteCString(string s, size_t maxLen) {
            assert(maxLen > 0);
            if (s.size() > maxLen - 1)
                s = s.substr(0, maxLen - 1);

            s += '\0';
            _stream.write(s.data(), s.size());
        }

        // Writes a fixed length string
        void WriteString(string s, size_t length) {
            assert(length > 0);

            for (size_t i = 0; i < length; i++) {
                if (i < s.length())
                    _stream.put(s[i]);
                else
                    _stream.put('\0');
            }
        }

        // Current stream position
        size_t Position() const { return _stream.tellp() - _start; }

        // Seek from the beginning
        void Seek(std::streampos offset) {
            _stream.seekp(_start + offset, std::ios_base::beg);
        }

        // Seek forward from the current position
        void SeekForward(size_t offset) {
            _stream.seekp(offset, std::ios_base::cur);
        }
    };
}