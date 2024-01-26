#include "pch.h"
#include "music.h"

namespace Inferno::Sound {
    Music LoadMp3(const List<byte>& mp3) {
        drmp3 decoder;
        Music music{};

        if (!drmp3_init_memory(&decoder, mp3.data(), mp3.size(), nullptr)) {
            return music;
        }

        uint64 samples = drmp3_get_pcm_frame_count(&decoder);

        if (!samples) {
            drmp3_uninit(&decoder);
            return music;
        }

        music.Data.resize(samples * decoder.channels);
        music.SampleRate = decoder.sampleRate;
        music.Channels = decoder.channels;
        music.Samples = samples;

        drmp3_seek_to_pcm_frame(&decoder, 0);

        for (size_t i = 0; i < samples; i += 512) {
            float buffer[512 * MAX_CHANNELS] = {};
            auto frames = std::min(samples - i, 512ull);
            drmp3_read_pcm_frames_f32(&decoder, frames, buffer);

            for (size_t f = 0; f < frames; f++) {
                for (uint ch = 0; ch < music.Channels; ch++) {
                    music.Data[(i + f) * music.Channels + ch] = buffer[f * music.Channels + ch];
                }
            }
        }

        drmp3_uninit(&decoder);

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

        //while (true) {
        //    float sampleBuffer[MAX_CHANNELS];
        //    auto samples = stb_vorbis_get_samples_float_interleaved(vorbis, music.Channels, sampleBuffer, music.Channels);
        //    if (samples == 0) break;

        //    for (uint ch = 0; ch < music.Channels; ch++)
        //        music.Data[sample * music.Channels + ch] = sampleBuffer[ch];

        //    sample++;
        //}

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

        //while (true) {
        //    float** outputs;
        //    int samples = stb_vorbis_get_frame_float(vorbis, nullptr, &outputs);
        //    stb_vorbis_get_samples();

        //    //short** outputs;
        //    //int n = stb_vorbis_get_frame_short(vorbis, c, outputs, samples);
        //    if (samples == 0)
        //        break;

        //    for (uint ch = 0; ch < 1 /*music.Channels*/; ch++) {
        //        if (offset + music.Samples * ch + sizeof(float) * samples >= music.Data.size()) {
        //            SPDLOG_WARN("Data read at song offset {} exceeds buffer size", offset);
        //            break;
        //        }
        //        //assert(offset + music.Samples * ch + sizeof(float) * n < music.Data.size());

        //        for (int sample = 0; sample < samples; sample++) {
        //            memcpy(music.Data.data() + offset + sample + ch * samples, outputs[ch], sizeof(float));
        //        }
        //        //memcpy(mData + offset + music.Samples * ch, outputs[ch], sizeof(float) * n);
        //    }

        //    offset += samples;
        //}

        stb_vorbis_close(vorbis);

        //for (int i = 0; i < music.Data.size() / sizeof(float); i++) {
        //    float f;
        //    memcpy(&f, music.Data.data() + i * sizeof(float), sizeof(float));
        //    assert(f >= -1 && f <= 1);
        //    short s = (int)(SHRT_MAX * f);
        //    memcpy(music.Data.data() + i * sizeof(short), &s, sizeof(short));
        //}

        //StreamReader reader(music.Data);
        //while (!reader.EndOfStream()) {
        //    auto f = reader.ReadFloat();
        //    assert(f >= -1 && f <= 1);
        //}

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
