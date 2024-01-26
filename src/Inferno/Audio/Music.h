#pragma once

#include "Audio/Audio.h"
#include "FileSystem.h"
#include "pcm/dr_flac.h"
#include "pcm/dr_mp3.h"
#include "pcm/stb_vorbis.h"
#include "SoundSystem.h"
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
    };

    class Mp3Stream : public MusicStream {
        drmp3 _decoder;
        List<byte> _source;
        uint _bufferIndex = 0;
        size_t _frames = 0;

    public:
        Mp3Stream(std::vector<byte>&& source) : _source(std::move(source)) {
            if (!drmp3_init_memory(&_decoder, _source.data(), _source.size(), nullptr)) {
                throw Exception("Unable to init drmp3");
            }

            _frames = drmp3_get_pcm_frame_count(&_decoder);

            if (!_frames) {
                drmp3_uninit(&_decoder);
                throw Exception("Empty or invalid MP3");
            }

            //drmp3_seek_to_pcm_frame(&_decoder, _frames - 500000);

            auto fillBuffer = [this](DynamicSoundEffectInstance* effect) {
                auto count = effect->GetPendingBufferCount();
                while (count < BUFFER_COUNT) {
                    auto& buffer = _buffer[_bufferIndex++];
                    _bufferIndex %= BUFFER_COUNT;

                    auto len = ReadNextFrame(buffer);
                    if (len == 0) {
                        if (Loop) drmp3_seek_to_pcm_frame(&_decoder, 0);
                        return; // out of data
                    }

                    effect->SubmitBuffer((uint8*)buffer.data(), len);
                    count++;
                }
            };

            Effect = std::make_unique<DynamicSoundEffectInstance>(
                GetEngine(), fillBuffer, _decoder.sampleRate, _decoder.channels, 32);
        }

        Mp3Stream(const Mp3Stream&) = delete;
        Mp3Stream(Mp3Stream&&) = default;
        Mp3Stream& operator=(const Mp3Stream&) = delete;
        Mp3Stream& operator=(Mp3Stream&&) = default;

        ~Mp3Stream() {
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
        OggStream(std::vector<byte>&& ogg) : _source(std::move(ogg)) {
            int e = 0;
            _vorbis = stb_vorbis_open_memory(_source.data(), (int)_source.size(), &e, nullptr);

            if (!_vorbis) {
                throw Exception("Unable to init stb vorbis");
            }

            _info = stb_vorbis_get_info(_vorbis);
            _frames = stb_vorbis_stream_length_in_samples(_vorbis);

            if (!_frames) {
                stb_vorbis_close(_vorbis);
                throw Exception("Empty or invalid OGG");
            }

            //stb_vorbis_seek(_vorbis, _frames - 200000);

            auto fillBuffer = [this](DynamicSoundEffectInstance* effect) {
                auto count = effect->GetPendingBufferCount();
                while (count < BUFFER_COUNT) {
                    auto& buffer = _buffer[_bufferIndex++];
                    _bufferIndex %= BUFFER_COUNT;

                    auto len = ReadNextFrame(buffer);
                    if (len == 0) {
                        if (Loop) stb_vorbis_seek(_vorbis, 0); // loop
                        return; // out of data
                    }

                    effect->SubmitBuffer((uint8*)buffer.data(), len);
                    count++;
                }
            };

            Effect = std::make_unique<DynamicSoundEffectInstance>(
                GetEngine(), fillBuffer, _info.sample_rate, _info.channels, 32);
        }

        OggStream(const OggStream&) = delete;
        OggStream(OggStream&&) = default;
        OggStream& operator=(const OggStream&) = delete;
        OggStream& operator=(OggStream&&) = default;

        ~OggStream() {
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
        size_t _frames = 0;

    public:
        FlacStream(std::vector<byte>&& ogg) : _source(std::move(ogg)) {
            _flac = drflac_open_memory(_source.data(), _source.size(), nullptr);

            if (!_flac) {
                throw Exception("Unable to init drflac");
            }

            if (!_flac->totalPCMFrameCount) {
                drflac_close(_flac);
                throw Exception("Empty or invalid FLAC");
            }

            //drflac_seek_to_pcm_frame(_flac, _flac->totalPCMFrameCount - 48'000 * 5);

            auto fillBuffer = [this](DynamicSoundEffectInstance* effect) {
                auto count = effect->GetPendingBufferCount();
                while (count < BUFFER_COUNT) {
                    auto& buffer = _buffer[_bufferIndex++];
                    _bufferIndex %= BUFFER_COUNT;

                    auto len = ReadNextFrame(buffer);
                    if (len == 0) {
                        if (Loop) drflac_seek_to_pcm_frame(_flac, 0); // loop
                        return; // out of data
                    }

                    effect->SubmitBuffer((uint8*)buffer.data(), len);
                    count++;
                }
            };

            Effect = std::make_unique<DynamicSoundEffectInstance>(
                GetEngine(), fillBuffer, _flac->sampleRate, _flac->channels, 32);
        }

        FlacStream(const FlacStream&) = delete;
        FlacStream(FlacStream&&) = default;
        FlacStream& operator=(const FlacStream&) = delete;
        FlacStream& operator=(FlacStream&&) = default;

        ~FlacStream() {
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
