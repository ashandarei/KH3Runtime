#include "kh02_movie_audio.h"
#include "kh02_optional_audio.h"
#include "shared/kh02/kh02_movie_shared.h"
#include <xaudio2.h>
#include <mmreg.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <stdexcept>

#pragma comment(lib, "xaudio2.lib")

namespace kh3ap
{
    namespace
    {
        auto movie_audio_engine() -> IXAudio2*
        {
            // As with custom music, retain the process engine to avoid releasing
            // XAudio2 from DLL_PROCESS_DETACH under the Windows loader lock.
            static IXAudio2* engine = []
            {
                IXAudio2* created{};
                if (FAILED(XAudio2Create(&created)))
                {
                    return static_cast<IXAudio2*>(nullptr);
                }
                IXAudio2MasteringVoice* master{};
                if (FAILED(created->CreateMasteringVoice(&master)))
                {
                    created->Release();
                    return static_cast<IXAudio2*>(nullptr);
                }
                return created;
            }();
            return engine;
        }
    } // namespace

    struct Kh02MovieAudio::Impl : IXAudio2VoiceCallback
    {
        struct Buffer
        {
            std::atomic<bool> busy{};
            std::array<float, kh02movie::audio_packet_frames * kh02movie::audio_max_channels> samples;
        };

        std::array<Buffer, kh02movie::audio_slot_count> buffers;
        IXAudio2SourceVoice* voice{};
        std::atomic<HRESULT> voice_error{S_OK};
        int channels{}, rate{};
        uint64_t submitted_frames{}, played_frames{};
        float gain{}, peak{};
        bool paused{};

        Impl(int channel_count, int sample_rate) : channels(channel_count), rate(sample_rate)
        {
            if ((channels != 1 && channels != 2 && channels != 6) || rate < 8000 || rate > 192000)
            {
                throw std::runtime_error("movie_audio_output_format");
            }
            WAVEFORMATEXTENSIBLE extended{};
            auto& format = extended.Format;
            format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            format.nChannels = static_cast<WORD>(channels);
            format.nSamplesPerSec = rate;
            format.wBitsPerSample = 32;
            format.nBlockAlign = static_cast<WORD>(channels * sizeof(float));
            format.nAvgBytesPerSec = rate * format.nBlockAlign;
            format.cbSize = sizeof(extended) - sizeof(format);
            extended.Samples.wValidBitsPerSample = 32;
            extended.dwChannelMask = channels == 6 ? 0x3f : channels == 2 ? 0x3 : 0x4;
            extended.SubFormat = {WAVE_FORMAT_IEEE_FLOAT, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
            auto* engine = movie_audio_engine();
            if (!engine || FAILED(engine->CreateSourceVoice(&voice, &format, 0, 1.0f, this)))
            {
                throw Kh02AudioOutputError("movie_audio_output_create");
            }
            voice->SetVolume(0);
            if (FAILED(voice->Start()))
            {
                stop();
                throw Kh02AudioOutputError("movie_audio_output_start");
            }
        }

        ~Impl()
        {
            stop();
        }

        void stop() noexcept
        {
            if (voice)
            {
                XAUDIO2_VOICE_STATE state{};
                voice->GetState(&state);
                played_frames = state.SamplesPlayed;
                voice->Stop();
                voice->DestroyVoice();
                voice = nullptr;
            }
        }

        void tick(kh02movie::SharedFrames& shared, float volume)
        {
            if (!voice || FAILED(voice_error.load(std::memory_order_relaxed)))
            {
                throw Kh02AudioOutputError("movie_audio_output_failed");
            }
            if (!std::isfinite(volume) || volume < 0 || volume > 1)
            {
                throw std::runtime_error("movie_audio_output_volume");
            }
            gain = volume;
            if (FAILED(voice->SetVolume(gain)))
            {
                throw Kh02AudioOutputError("movie_audio_output_volume_failed");
            }
            auto& info = shared.header();
            if (kh02movie::read(info.audio_error))
            {
                throw std::runtime_error("movie_audio_shared_failed");
            }
            for (auto& buffer : buffers)
            {
                if (buffer.busy.load(std::memory_order_acquire))
                {
                    continue;
                }
                uint32_t frames{};
                if (!shared.consume_audio(buffer.samples, frames))
                {
                    break;
                }
                for (uint32_t index = 0; index < frames * channels; ++index)
                {
                    if (!std::isfinite(buffer.samples[index]))
                    {
                        throw std::runtime_error("movie_audio_nonfinite_sample");
                    }
                    peak = std::max(peak, std::abs(buffer.samples[index]));
                }
                XAUDIO2_BUFFER queued{};
                queued.AudioBytes = frames * channels * sizeof(float);
                queued.pAudioData = reinterpret_cast<const BYTE*>(buffer.samples.data());
                queued.pContext = &buffer;
                buffer.busy.store(true, std::memory_order_release);
                if (FAILED(voice->SubmitSourceBuffer(&queued)))
                {
                    buffer.busy.store(false, std::memory_order_release);
                    throw Kh02AudioOutputError("movie_audio_output_submit");
                }
                submitted_frames += frames;
            }
        }

        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}

        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}

        void STDMETHODCALLTYPE OnStreamEnd() override {}

        void STDMETHODCALLTYPE OnBufferStart(void*) override {}

        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}

        void STDMETHODCALLTYPE OnBufferEnd(void* context) override
        {
            static_cast<Buffer*>(context)->busy.store(false, std::memory_order_release);
        }

        void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT error) override
        {
            voice_error.store(error, std::memory_order_relaxed);
        }

        auto state() const -> XAUDIO2_VOICE_STATE
        {
            XAUDIO2_VOICE_STATE result{};
            if (voice)
            {
                voice->GetState(&result);
            }
            else
            {
                result.SamplesPlayed = played_frames;
            }
            return result;
        }
    };

    Kh02MovieAudio::Kh02MovieAudio(int channels, int rate) : impl(std::make_unique<Impl>(channels, rate)) {}

    Kh02MovieAudio::~Kh02MovieAudio() = default;

    void Kh02MovieAudio::tick(kh02movie::SharedFrames& shared, float gain)
    {
        impl->tick(shared, gain);
    }

    void Kh02MovieAudio::stop() noexcept
    {
        impl->stop();
    }

    void Kh02MovieAudio::set_paused(bool paused)
    {
        if (!impl->voice || impl->paused == paused)
        {
            return;
        }
        const auto result = paused ? impl->voice->Stop() : impl->voice->Start();
        if (FAILED(result))
        {
            throw Kh02AudioOutputError("movie_audio_output_pause_failed");
        }
        impl->paused = paused;
    }

    auto Kh02MovieAudio::drained(kh02movie::SharedFrames& shared) const -> bool
    {
        const auto& info = shared.header();
        return kh02movie::read(info.audio_published) == kh02movie::read(info.audio_consumed) &&
               impl->state().BuffersQueued == 0;
    }

    auto Kh02MovieAudio::snapshot() const -> nlohmann::json
    {
        const auto state = impl->state();
        return {{"backend", "kh3_xaudio2"},
                {"channels", impl->channels},
                {"sample_rate", impl->rate},
                {"submitted_frames", impl->submitted_frames},
                {"played_frames", state.SamplesPlayed},
                {"queued_buffers", state.BuffersQueued},
                {"gain", impl->gain},
                {"peak", impl->peak},
                {"active", impl->voice != nullptr},
                {"paused", impl->paused},
                {"payload_files_written", 0}};
    }

    auto Kh02MovieAudio::played_frames() const -> uint64_t
    {
        return impl->state().SamplesPlayed;
    }
} // namespace kh3ap
