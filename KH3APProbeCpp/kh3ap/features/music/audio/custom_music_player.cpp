#include "kh3ap/features/music/audio/custom_music_player.h"

#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/features/music/audio/custom_audio.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <Windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <chrono>
#include <format>
#include "kh3ap/features/music/audio/music_decode_queue.h"
#include "kh3ap/features/music/events/music_update_events.h"
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/utilities.h"
#include <sstream>
#endif

#pragma comment(lib, "xaudio2.lib")

using namespace RC;

namespace kh3ap
{
    namespace
    {
        using Decode = MusicDecodeCompletion<std::optional<CustomAudioData>>;
        auto start_loading(size_t channel, const std::filesystem::path& file)
            -> std::shared_ptr<Decode>
        {
            static MusicDecodeQueue<std::optional<CustomAudioData>,
                                    RuntimeCustomMusicPlayer::channel_count>
                queue{[](const std::filesystem::path& path, std::stop_token cancellation)
                      {
                          (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
                          const auto start = std::chrono::steady_clock::now();
                          auto audio = load_custom_audio(path, cancellation);
                          if (audio)
                          {
                              audio->load_time =
                                  std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - start);
                          }
                          return audio;
                      },
                      []
                      {
                          request_music_update(MusicUpdate::Decoded);
                      }};
            return queue.submit(channel, file);
        }

        // One engine for the process. It is deliberately never released:
        // tearing XAudio2 down from static destruction runs under the loader
        // lock at DLL_PROCESS_DETACH and can hang the game on exit.
        auto engine() -> IXAudio2*
        {
            static IXAudio2* instance = []() -> IXAudio2*
            {
                IXAudio2* created{};
                if (FAILED(XAudio2Create(&created, 0, XAUDIO2_DEFAULT_PROCESSOR)))
                {
                    return nullptr;
                }
                IXAudio2MasteringVoice* master{};
                if (FAILED(created->CreateMasteringVoice(&master)))
                {
                    created->Release();
                    return nullptr;
                }
                return created;
            }();
            return instance;
        }

        struct Channel
        {
            auto stop() -> void
            {
                if (voice)
                {
                    voice->Stop();
                    voice->DestroyVoice();
                    voice = nullptr;
                }
                wav.reset();
                file.clear();
                playing = false;
                if (loading)
                {
                    loading->cancel();
                }
                loading.reset();
                loading_file.clear();
            }

            IXAudio2SourceVoice* voice{};
            // Fade: the level now (0 silent to 1) and where it goes; a held
            // channel stops its voice once silent.
            float level{};
            float target{};
            bool holding{};
            std::shared_ptr<Decode> loading{};
            std::filesystem::path loading_file{};
            // Not retried until the next level: see stop_all().
            std::filesystem::path failed_file{};
            // Held while the voice reads from it.
            std::shared_ptr<const CustomAudioData> wav{};
            std::filesystem::path file{};
            float gain{1.0f};
            bool playing{};
        };

    } // namespace

    struct RuntimeCustomMusicPlayer::Impl
    {
        auto channel(size_t index) -> Channel*
        {
            return index < channels.size() ? &channels[index] : nullptr;
        }

        struct Decoded
        {
            std::filesystem::path file{};
            std::filesystem::file_time_type written{};
            std::shared_ptr<const CustomAudioData> wav{};
        };

        static auto written(const std::filesystem::path& file) -> std::filesystem::file_time_type
        {
            std::error_code error{};
            const auto time = std::filesystem::last_write_time(file, error);
            return error ? std::filesystem::file_time_type{} : time;
        }

        // A file decoded before and unchanged since.
        auto cached(const std::filesystem::path& file) -> std::shared_ptr<const CustomAudioData>
        {
            const auto found = std::ranges::find_if(decoded,
                                                    [&file](const Decoded& entry)
                                                    {
                                                        return entry.file == file;
                                                    });
            if (found == decoded.end())
            {
                return nullptr;
            }
            if (found->written != written(file))
            {
                decoded.erase(found);
                return nullptr;
            }
            // Most recent last.
            std::rotate(found, found + 1, decoded.end());
            return decoded.back().wav;
        }

        auto remember(const std::filesystem::path& file, std::shared_ptr<const CustomAudioData> wav)
            -> void
        {
            std::erase_if(decoded,
                          [&file](const Decoded& entry)
                          {
                              return entry.file == file;
                          });
            if (decoded.size() >= decoded_kept)
            {
                decoded.erase(decoded.begin());
            }
            decoded.push_back({file, written(file), std::move(wav)});
        }

        // A track plays again at every fight or level: decoded once, not each
        // time. A few minutes of 16-bit stereo is ~40 MB, so only the latest
        // few files are kept.
        static constexpr size_t decoded_kept = 4;
        std::vector<Decoded> decoded{};

        // A stopped voice fading out before it is destroyed; the samples it
        // reads stay alive until then.
        struct Retiring
        {
            IXAudio2SourceVoice* voice{};
            std::shared_ptr<const CustomAudioData> wav{};
            float gain{};
            float level{};
        };

        auto retire(Channel& channel) -> void
        {
            begin_fade();
            if (channel.voice && channel.playing && channel.level > 0.0f)
            {
                retiring.push_back({channel.voice, channel.wav, channel.gain, channel.level});
                channel.voice = nullptr;
            }
            channel.stop();
        }

        auto apply(IXAudio2SourceVoice* voice, float gain, float level) const -> void
        {
            (void)voice->SetVolume(volume * gain * level);
        }

        auto begin_fade() -> void
        {
            if (!fading)
            {
                last_update = std::chrono::steady_clock::now();
            }
            fading = true;
        }
        bool fading{};
        std::vector<Retiring> retiring{};
        std::array<Channel, channel_count> channels{};
        // The master volume (settings and menu ducking) and where it goes.
        float volume{1.0f};
        float volume_target{1.0f};
        std::chrono::steady_clock::time_point last_update{std::chrono::steady_clock::now()};
    };

    RuntimeCustomMusicPlayer::RuntimeCustomMusicPlayer() : impl_(std::make_unique<Impl>())
    {
    }

    RuntimeCustomMusicPlayer::~RuntimeCustomMusicPlayer()
    {
        // Same reason as engine(): at process exit the voices and the samples
        // they read are left alive rather than torn down.
        const bool voices =
            !impl_->retiring.empty() || std::ranges::any_of(impl_->channels,
                                                            [](const Channel& channel)
                                                            {
                                                                return channel.voice != nullptr;
                                                            });
        if (voices)
        {
            (void)impl_.release();
        }
    }

    auto RuntimeCustomMusicPlayer::play(size_t index, const CustomMusicTrack& track) -> bool
    {
        const std::filesystem::path& file = track.file;
        Channel* const channel = impl_->channel(index);
        if (!channel)
        {
            return false;
        }
        if (channel->voice && channel->file == file)
        {
            if (channel->gain != track.volume)
            {
                channel->gain = track.volume;
                impl_->apply(channel->voice, channel->gain, channel->level);
            }
            // Back from a hold: fades in again, from where the fade out got.
            channel->holding = false;
            channel->target = 1.0f;
            if (channel->level != channel->target)
            {
                impl_->begin_fade();
            }
            if (!channel->playing && SUCCEEDED(channel->voice->Start()))
            {
                channel->playing = true;
            }
            return channel->playing;
        }
        if (file == channel->failed_file)
        {
            // A different file may have played since this failure. Do not
            // leave that old voice playing under the restored native music.
            impl_->retire(*channel);
            return false;
        }
        std::shared_ptr<const CustomAudioData> wav{};
        std::chrono::milliseconds load_time{-1};
        if (channel->loading_file != file)
        {
            impl_->retire(*channel);
            wav = impl_->cached(file);
            if (!wav)
            {
                channel->loading = start_loading(index, file);
                channel->loading_file = file;
                return false;
            }
        }
        else
        {
            auto completed = channel->loading->take();
            if (!completed)
            {
                return false;
            }
            auto loaded = std::move(*completed);
            channel->loading.reset();
            channel->loading_file.clear();
            if (loaded)
            {
                load_time = loaded->load_time;
                wav = std::make_shared<const CustomAudioData>(std::move(*loaded));
                impl_->remember(file, wav);
            }
        }
        IXAudio2* const audio = engine();
        if (!wav || !audio)
        {
            channel->failed_file = file;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Custom music unreadable | file={} decoded={} xaudio2={}\n"),
                file.wstring(), wav != nullptr, audio != nullptr);
            return false;
        }
        channel->wav = std::move(wav);
        const auto* const format =
            reinterpret_cast<const WAVEFORMATEX*>(channel->wav->format.data());
        const auto [loop_begin, loop_length] =
            custom_music_loop_region(track.loop_start, track.loop_end, format->nSamplesPerSec,
                                     channel->wav->samples.size() / format->nBlockAlign);
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(channel->wav->samples.size());
        buffer.pAudioData = reinterpret_cast<const BYTE*>(channel->wav->samples.data());
        buffer.LoopBegin = loop_begin;
        buffer.LoopLength = loop_length;
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        channel->gain = track.volume;
        // Fades in, as the game's music it replaces fades out.
        channel->level = 0.0f;
        channel->target = 1.0f;
        impl_->begin_fade();
        channel->holding = false;
        if (FAILED(audio->CreateSourceVoice(&channel->voice, format)) ||
            FAILED(channel->voice->SubmitSourceBuffer(&buffer)) ||
            FAILED(channel->voice->SetVolume(0.0f)) || FAILED(channel->voice->Start()))
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Custom music format rejected | "
                                                "file={} tag={} channels={} rate={} bits={}\n"),
                                            file.wstring(), format->wFormatTag, format->nChannels,
                                            format->nSamplesPerSec, format->wBitsPerSample);
            channel->stop();
            channel->failed_file = file;
            return false;
        }
        channel->file = file;
        channel->playing = true;
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Custom music playing | channel={} file={} loop_begin={} "
                "loop_length={} volume={} {}\n"),
            index, file.wstring(), loop_begin, loop_length, channel->gain,
            load_time.count() < 0 ? std::wstring{STR("cached")}
                                  : std::format(STR("decoded_ms={}"), load_time.count()));
        return true;
    }

    auto RuntimeCustomMusicPlayer::hold(size_t index) -> void
    {
        Channel* const channel = impl_->channel(index);
        // Fades out, then update() stops the voice: Stop keeps the play
        // cursor, so a later Start resumes where it was.
        if (channel && channel->voice && channel->playing)
        {
            channel->holding = true;
            channel->target = 0.0f;
            impl_->begin_fade();
        }
    }

    auto RuntimeCustomMusicPlayer::stop(size_t index) -> void
    {
        Channel* const channel = impl_->channel(index);
        if (!channel || (!channel->voice && channel->loading_file.empty()))
        {
            return;
        }
        if (channel->voice)
        {
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Custom music stopped | channel={} file={}\n"), index,
                channel->file.wstring());
        }
        impl_->retire(*channel);
    }

    auto RuntimeCustomMusicPlayer::stop_all() -> void
    {
        for (size_t index = 0; index < channel_count; ++index)
        {
            stop(index);
            // A new level gives unreadable files another try.
            impl_->channels[index].failed_file.clear();
        }
    }

    auto RuntimeCustomMusicPlayer::file(size_t index) const -> const std::filesystem::path&
    {
        static const std::filesystem::path none{};
        return index < channel_count ? impl_->channels[index].file : none;
    }

    auto RuntimeCustomMusicPlayer::set_volume(float volume) -> void
    {
        if (impl_->volume_target != volume)
        {
            impl_->begin_fade();
        }
        impl_->volume_target = volume;
    }

    auto RuntimeCustomMusicPlayer::active() const -> bool
    {
        return !impl_->retiring.empty() ||
               std::ranges::any_of(impl_->channels,
                                   [](const Channel& channel)
                                   {
                                       return channel.voice || !channel.loading_file.empty();
                                   });
    }

    auto RuntimeCustomMusicPlayer::update() -> void
    {
        if (!impl_->fading)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        const float step =
            std::chrono::duration<float>(now - impl_->last_update).count() / fade_seconds;
        impl_->last_update = now;
        const auto toward = [step](float value, float target)
        {
            return value < target ? (std::min)(value + step, target)
                                  : (std::max)(value - step, target);
        };
        const bool volume_moved = impl_->volume != impl_->volume_target;
        impl_->volume = toward(impl_->volume, impl_->volume_target);
        for (Channel& channel : impl_->channels)
        {
            if (!channel.voice || !channel.playing)
            {
                continue;
            }
            if (channel.level != channel.target || volume_moved)
            {
                channel.level = toward(channel.level, channel.target);
                impl_->apply(channel.voice, channel.gain, channel.level);
            }
            if (channel.holding && channel.level <= 0.0f && SUCCEEDED(channel.voice->Stop()))
            {
                channel.playing = false;
            }
        }
        std::erase_if(impl_->retiring,
                      [&](Impl::Retiring& voice)
                      {
                          voice.level = toward(voice.level, 0.0f);
                          if (voice.level > 0.0f)
                          {
                              impl_->apply(voice.voice, voice.gain, voice.level);
                              return false;
                          }
                          voice.voice->Stop();
                          voice.voice->DestroyVoice();
                          return true;
                      });
        impl_->fading = !impl_->retiring.empty() || impl_->volume != impl_->volume_target ||
                        std::ranges::any_of(impl_->channels,
                                            [](const Channel& channel)
                                            {
                                                return channel.voice && channel.playing &&
                                                       channel.level != channel.target;
                                            });
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeCustomMusicPlayer::test_snapshot() const -> std::string
    {
        std::ostringstream out;
        out << std::boolalpha << "{\"active\":" << active()
            << ",\"retiring\":" << impl_->retiring.size() << ",\"volume\":" << impl_->volume
            << ",\"channels\":[";
        for (size_t index = 0; index < impl_->channels.size(); ++index)
        {
            const Channel& channel = impl_->channels[index];
            XAUDIO2_VOICE_STATE state{};
            float volume{};
            if (channel.voice)
            {
                channel.voice->GetState(&state);
                channel.voice->GetVolume(&volume);
            }
            if (index)
            {
                out << ',';
            }
            out << "{\"index\":" << index << ",\"file\":\"" << json_escape(channel.file.wstring())
                << "\",\"loading\":\"" << json_escape(channel.loading_file.wstring())
                << "\",\"failed\":\"" << json_escape(channel.failed_file.wstring())
                << "\",\"playing\":" << channel.playing << ",\"holding\":" << channel.holding
                << ",\"samples\":" << state.SamplesPlayed << ",\"buffers\":" << state.BuffersQueued
                << ",\"voice_volume\":" << volume << '}';
        }
        out << "]}";
        return out.str();
    }
#endif

    auto custom_music_directory() -> std::filesystem::path
    {
        const auto dlls = try_get_current_module_directory();
        return dlls ? configured_custom_music_directory(dlls->parent_path() / "custom_music")
                    : std::filesystem::path{};
    }
} // namespace kh3ap
