#pragma once
#include "Types.h"
#include "Streams.h"

namespace Inferno {
    // All multi - byte numbers are stored in little endian format.
    //
    // In Descent 1 retail all sound files are stored in DESCENT.PIG file except for one file: DIGITEST.RAW which is in DESCENT.HOG.
    // Audio is formatted as uncompressed 8-bit unsigned mono PCM data. All sound files in Descent 1 .PIG should be played at 11025 Hz.
    //
    // Descent 1 shareware audio is compressed.
    //
    // In Descent 2 all sound files are stored in DESCENT2.S11 and DESCENT2.S22 except for DIGITEST.RAW which is in DESCENT2.HOG.
    // Each file in .S11/.S22 is 8-bit unsigned mono PCM data. PCM data from .S11 should be played at 11025 Hz, while PCM data from .S22 should be played at 22050 Hz.
    
    struct SoundFile {
        struct Header {
            string Name;
            int Length; // Uncompressed data length. Equals DataLength for retail.
            int DataLength; // File data length
            int Offset;
        };

        wstring Path;
        List<Header> Sounds; // entries
        int Frequency = 22050; // 22050 Hz for S22, 11025 Hz for S11
        size_t DataStart = 0u;

        List<ubyte> Read(int index) const;
    };

    SoundFile::Header ReadSoundHeader(StreamReader& reader);

    // Reads a S11 or S22 file. This can be modified to read from a PIG file for Descent 1.
    SoundFile ReadSoundFile(wstring path);
}