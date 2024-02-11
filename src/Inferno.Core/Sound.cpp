#include "pch.h"

#include "Sound.h"
#include "Streams.h"

namespace Inferno {
    constexpr std::array INDEX_TABLE = {
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    };

    constexpr std::array STEP_TABLE = {
        7, 8, 9, 10, 11, 12, 13, 14,
        16, 17, 19, 21, 23, 25, 28,
        31, 34, 37, 41, 45, 50, 55,
        60, 66, 73, 80, 88, 97, 107,
        118, 130, 143, 157, 173, 190, 209,
        230, 253, 279, 307, 337, 371, 408,
        449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552,
        1707, 1878,
        2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
        4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818,
        18500, 20350, 22385, 24623, 27086, 29794, 32767
    };

    void DecompressSound(span<const byte> data, span<byte> output) {
        int token = 1;
        int predicted = 0, index = 0, step = 7;
        int offset = 0;
        auto iterator = data.begin();

        while (iterator != data.end()) {
            int code = token ? *iterator & 15 : *iterator++ >> 4;
            token ^= 1;
            int diff = 0;

            if (code & 4)
                diff += step;
            if (code & 2)
                diff += (step >> 1);
            if (code & 1)
                diff += (step >> 2);

            diff += (step >> 3);
            if (code & 8)
                diff = -diff;

            predicted = std::clamp(predicted + diff, INT16_MIN, (int)INT16_MAX);
            output[offset++] = ((predicted >> 8) & 255) ^ 0x80;
            if (offset >= output.size()) return; // prevent buffer overrun

            index += INDEX_TABLE[code];
            index = std::clamp(index, 0, (int)STEP_TABLE.size() - 1);
            step = STEP_TABLE[index];
        }
    }


    SoundFile::Header ReadSoundHeader(StreamReader& reader) {
        SoundFile::Header header;
        header.Name = reader.ReadString(8);
        header.Length = reader.ReadInt32();
        header.DataLength = reader.ReadInt32();
        header.Offset = reader.ReadInt32();
        return header;
    }

    SoundFile ReadSoundFile(const filesystem::path& path) {
        StreamReader reader(path);
        auto id = reader.ReadInt32();
        auto version = reader.ReadInt32();
        if (id != MakeFourCC("DSND") || version != 1)
            throw Exception("Invalid sound file");

        SoundFile file;
        file.Path = path;
        file.Sounds.resize(reader.ReadInt32());

        for (auto& sound : file.Sounds)
            sound = ReadSoundHeader(reader);

        file.DataStart = reader.Position();
        return file;
    }

    List<ubyte> SoundFile::Read(int index) const {
        if (!Seq::inRange(Sounds, index)) return {};
        StreamReader reader(Path);
        auto& sound = Sounds[index];

        //SPDLOG_INFO("Reading header {} ID: {}", header.Name, id);
        reader.Seek(DataStart + sound.Offset);
        return reader.ReadUBytes(sound.Length);
    }

    List<ubyte> SoundFile::ReadCompressed(int index) const {
        if (!Seq::inRange(Sounds, index)) return {};
        StreamReader reader(Path);
        auto& sound = Sounds[index];

        //SPDLOG_INFO("Reading header {} ID: {}", header.Name, id);
        reader.Seek(DataStart + sound.Offset);
        auto compressed = reader.ReadUBytes(sound.DataLength);

        List<byte> buffer;
        buffer.resize(sound.Length);

        DecompressSound(compressed, buffer);
        return buffer;
    }

    Option<size_t> SoundFile::Find(string_view name) const {
        if (auto index = name.find('.'); index != -1)
            name = name.substr(0, index);

        return Seq::findIndex(Sounds, [&name](const Header& h) { return h.Name == name; });
    }
}
