#include "pch.h"

#include "Sound.h"
#include "Streams.h"

namespace Inferno {
    SoundFile::Header ReadSoundHeader(StreamReader& reader) {
        SoundFile::Header header;
        header.Name = reader.ReadString(8);
        header.Length = reader.ReadInt32();
        header.DataLength = reader.ReadInt32();
        header.Offset = reader.ReadInt32();
        return header;
    }

    SoundFile ReadSoundFile(wstring path) {
        StreamReader reader(path);
        auto id = reader.ReadInt32();
        auto version = reader.ReadInt32();
        if (id != 'DNSD' || version != 1)
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
}