#pragma once

#include "Audio/Audio.h"
#include "pcm/dr_flac.h"
#include "pcm/dr_mp3.h"
#include "pcm/stb_vorbis.h"
#include "Types.h"

namespace Inferno::Sound {
    constexpr uint MAX_CHANNELS = 2;

    struct Music {
        List<float> Data;
        uint SampleRate;
        uint64 Samples;
        uint Channels;
    };

    class MusicStream {
    protected:
        static constexpr size_t CHUNK_SIZE = 512;
        static constexpr auto BUFFER_COUNT = 3;
        using FrameBuffer = std::array<float, CHUNK_SIZE * MAX_CHANNELS>;
        Array<FrameBuffer, BUFFER_COUNT> _buffer{};

    public:
        Ptr<DynamicSoundEffectInstance> Effect;
        bool Loop = true;
        float Length = 0; // Length in seconds

        MusicStream() = default;
        MusicStream(const MusicStream&) = delete;
        MusicStream(MusicStream&&) = default;
        virtual MusicStream& operator=(const MusicStream&) = delete;
        virtual MusicStream& operator=(MusicStream&&) = default;
        virtual ~MusicStream() = default;
    };

    class Mp3Stream : public MusicStream {
        drmp3 _decoder;
        List<byte> _source;
        uint _bufferIndex = 0;
        size_t _frames = 0;

    public:
        Mp3Stream(std::vector<byte>&& source);

        ~Mp3Stream() override {
            drmp3_uninit(&_decoder);
        }

    private:
        // Returns the size in bytes
        size_t ReadNextFrame(FrameBuffer& dest) {
            auto frames = drmp3_read_pcm_frames_f32(&_decoder, CHUNK_SIZE, dest.data());
            return frames * _decoder.channels * sizeof(float);
        }
    };

    class OggStream : public MusicStream {
        stb_vorbis* _vorbis;
        List<byte> _source;
        uint _bufferIndex = 0;
        size_t _frames = 0;
        stb_vorbis_info _info{};

    public:
        OggStream(std::vector<byte>&& ogg);

        ~OggStream() override {
            stb_vorbis_close(_vorbis);
        }

    private:
        // Returns the size in bytes
        size_t ReadNextFrame(FrameBuffer& dest) const {
            auto frames = stb_vorbis_get_samples_float_interleaved(_vorbis, _info.channels, dest.data(), CHUNK_SIZE);
            return frames * _info.channels * sizeof(float);
        }
    };

    class FlacStream : public MusicStream {
        drflac* _flac;
        List<byte> _source;
        uint _bufferIndex = 0;

    public:
        FlacStream(std::vector<byte>&& ogg);

        ~FlacStream() override {
            drflac_close(_flac);
        }

    private:
        // Returns the size in bytes
        size_t ReadNextFrame(FrameBuffer& dest) const {
            auto frames = drflac_read_pcm_frames_f32(_flac, CHUNK_SIZE, dest.data());
            return frames * _flac->channels * sizeof(float);
        }
    };

    inline Music LoadMp3(const List<byte>& mp3);

    inline Music LoadFlac(const List<byte>& flac);

    Music LoadOgg(const List<byte>& ogg);

    Music LoadMusic(const string& file);

}
