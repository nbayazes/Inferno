#include "pch.h"
#include "music.h"
#include "SoundSystem.h"
#include "FileSystem.h"

namespace Inferno::Sound {
    Mp3Stream::Mp3Stream(std::vector<byte>&& source): _source(std::move(source)) {
        if (!drmp3_init_memory(&_decoder, _source.data(), _source.size(), nullptr)) {
            throw Exception("Unable to init drmp3");
        }

        _frames = drmp3_get_pcm_frame_count(&_decoder);

        if (_frames == 0) {
            drmp3_uninit(&_decoder);
            throw Exception("Empty or invalid MP3");
        }
        
        Length = (float)_frames / _decoder.mp3FrameSampleRate;

        //drmp3_seek_to_pcm_frame(&_decoder, _frames - 500000);

        auto fillBuffer = [this](DynamicSoundEffectInstance* effect) {
            auto count = effect->GetPendingBufferCount();
            while (count < BUFFER_COUNT) {
                auto& buffer = _buffer[_bufferIndex++];
                _bufferIndex %= BUFFER_COUNT;

                auto len = ReadNextFrame(buffer);
                if (len == 0) {
                    if (Loop) {
                        drmp3_seek_to_pcm_frame(&_decoder, 0);
                        len = ReadNextFrame(buffer);
                    }

                    if (len == 0)
                        return; // out of data or didn't loop
                }

                effect->SubmitBuffer((uint8*)buffer.data(), len);
                count++;
            }
        };

        Effect = std::make_unique<DynamicSoundEffectInstance>(
            GetEngine(), fillBuffer, _decoder.sampleRate, _decoder.channels, 32);
    }

    OggStream::OggStream(std::vector<byte>&& ogg): _source(std::move(ogg)) {
        int e = 0;
        _vorbis = stb_vorbis_open_memory(_source.data(), (int)_source.size(), &e, nullptr);

        if (!_vorbis) {
            throw Exception("Unable to init stb vorbis");
        }

        _info = stb_vorbis_get_info(_vorbis);
        _frames = stb_vorbis_stream_length_in_samples(_vorbis);
        Length = stb_vorbis_stream_length_in_seconds(_vorbis);

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
                    if (Loop) {
                        stb_vorbis_seek(_vorbis, 0); // loop
                        len = ReadNextFrame(buffer);
                    }

                    if (len == 0)
                        return; // out of data or didn't loop
                }

                effect->SubmitBuffer((uint8*)buffer.data(), len);
                count++;
            }
        };

        Effect = std::make_unique<DynamicSoundEffectInstance>(
            GetEngine(), fillBuffer, _info.sample_rate, _info.channels, 32);
    }

    FlacStream::FlacStream(std::vector<byte>&& ogg): _source(std::move(ogg)) {
        _flac = drflac_open_memory(_source.data(), _source.size(), nullptr);

        if (!_flac) {
            throw Exception("Unable to init drflac");
        }

        if (!_flac->totalPCMFrameCount) {
            drflac_close(_flac);
            throw Exception("Empty or invalid FLAC");
        }

        Length = (float)_flac->totalPCMFrameCount / _flac->sampleRate;

        //drflac_seek_to_pcm_frame(_flac, _flac->totalPCMFrameCount - 48'000 * 5);

        auto fillBuffer = [this](DynamicSoundEffectInstance* effect) {
            auto count = effect->GetPendingBufferCount();
            while (count < BUFFER_COUNT) {
                auto& buffer = _buffer[_bufferIndex++];
                _bufferIndex %= BUFFER_COUNT;

                auto len = ReadNextFrame(buffer);
                if (len == 0) {
                    if (Loop) {
                        drflac_seek_to_pcm_frame(_flac, 0); // loop
                        len = ReadNextFrame(buffer);
                    }

                    if (len == 0)
                        return; // out of data or didn't loop
                }

                effect->SubmitBuffer((uint8*)buffer.data(), len);
                count++;
            }
        };

        Effect = std::make_unique<DynamicSoundEffectInstance>(
            GetEngine(), fillBuffer, _flac->sampleRate, _flac->channels, 32);
    }

    Music LoadMp3(const List<byte>& mp3) {
        auto decoder = std::unique_ptr<drmp3, decltype([](drmp3* p) {
            drmp3_uninit(p);
            std::default_delete<drmp3>()(p);
        })>{};

        Music music{};

        if (!drmp3_init_memory(decoder.get(), mp3.data(), mp3.size(), nullptr)) {
            return music;
        }

        uint64 samples = drmp3_get_pcm_frame_count(decoder.get());

        if (!samples) {
            return music;
        }

        music.Data.resize(samples * decoder->channels);
        music.SampleRate = decoder->sampleRate;
        music.Channels = decoder->channels;
        music.Samples = samples;

        drmp3_seek_to_pcm_frame(decoder.get(), 0);

        for (size_t i = 0; i < samples; i += 512) {
            float buffer[512 * MAX_CHANNELS] = {};
            auto frames = std::min(samples - i, 512ull);
            drmp3_read_pcm_frames_f32(decoder.get(), frames, buffer);

            for (size_t f = 0; f < frames; f++) {
                for (uint ch = 0; ch < music.Channels; ch++) {
                    music.Data[(i + f) * music.Channels + ch] = buffer[f * music.Channels + ch];
                }
            }
        }

        return music;
    }

    Music LoadFlac(const List<byte>& flac) {
        auto decoder = drflac_open_memory(flac.data(), flac.size(), nullptr);
        Music music{};

        if (!decoder)
            return music;

        uint64 samples = decoder->totalPCMFrameCount;

        if (!samples) {
            drflac_close(decoder);
            return music;
        }

        music.Data.resize(samples * decoder->channels);
        music.SampleRate = decoder->sampleRate;
        music.Channels = decoder->channels;
        music.Samples = samples;
        drflac_seek_to_pcm_frame(decoder, 0);

        for (size_t i = 0; i < samples; i += 512) {
            float buffer[512 * MAX_CHANNELS] = {};
            auto frames = std::min(samples - i, 512ull);
            drflac_read_pcm_frames_f32(decoder, frames, buffer);

            for (size_t f = 0; f < frames; f++) {
                for (uint ch = 0; ch < music.Channels; ch++) {
                    music.Data[(i + f) * music.Channels + ch] = buffer[f * music.Channels + ch];
                }
            }
        }

        drflac_close(decoder);
        return music;
    }

    Music LoadOgg(const List<byte>& ogg) {
        Music music{};
        int e = 0;
        auto vorbis = stb_vorbis_open_memory(ogg.data(), (int)ogg.size(), &e, nullptr);

        if (!vorbis) {
            return music;
        }

        stb_vorbis_info info = stb_vorbis_get_info(vorbis);
        music.SampleRate = info.sample_rate;
        music.Channels = info.channels;
        music.Samples = stb_vorbis_stream_length_in_samples(vorbis);
        music.Data.resize(music.Samples * music.Channels);

        int offset = 0;

        while (true) {
            float** buffer;
            int frames = stb_vorbis_get_frame_float(vorbis, nullptr, &buffer);
            if (frames == 0) break;

            for (int f = 0; f < frames; f++) {
                for (uint ch = 0; ch < music.Channels; ch++) {
                    music.Data[offset + music.Channels * f + ch] = buffer[ch][f];
                }
            }

            offset += frames * music.Channels;
        }

        stb_vorbis_close(vorbis);

        return music;
    }

    Music LoadMusic(const string& file) {
        auto bytes = File::ReadAllBytes(file);
        if (bytes.empty()) return {};

        uint32 header;
        memcpy(&header, bytes.data(), sizeof(uint32));

        switch (header) {
            case MakeFourCC("OggS"):
                return LoadOgg(bytes);

            case MakeFourCC("RIFF"):
                //music = LoadWav(bytes);
                return {};

            case MakeFourCC("fLaC"):
                return LoadFlac(bytes);

            default:
                return LoadMp3(bytes);
        }
    }
}
