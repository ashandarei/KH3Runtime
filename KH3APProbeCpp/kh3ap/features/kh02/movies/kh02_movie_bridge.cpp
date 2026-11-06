#include "kh02_movie_decoder_lease.h"
#include "kh02_movie_frame_presenter.h"
#include "kh02_movie_playback_state.h"
#include "kh02_movie_source_continuations.h"
#include "kh02_movie_frame_queue.h"
#include "kh02_movie_request_plan.h"
#include "kh02_movie_settings.h"
#include "kh02_movie_bridge.h"
#include "kh02_movie_texture.h"
#include "kh02_movie_surface.h"
#include "kh02_movie_audio.h"
#include "kh02_optional_audio.h"
#include "kh3ap/features/kh02/core/kh02_voice_language.h"
#include "shared/kh02/kh02_movie_shared.h"
#include "kh3ap/features/kh02/transport/kh02_asset_companion.h"
#include "kh02/story_movie_metadata.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 512;
        constexpr std::size_t history_event_limit = 128;
        constexpr std::string_view gear_movie_prefix = "Rebellis02/gear_";
        constexpr auto preparation_timeout = std::chrono::seconds(30);
        constexpr auto playback_timeout = std::chrono::minutes(40);
        constexpr int maximum_movie_frame_count = 60'000;
        constexpr double minimum_movie_fps = 20;
        constexpr double maximum_movie_fps = 60;

        struct Call : Kh02ReflectedCall
        {
            Call(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "movie_bridge", reflected_parameter_limit_bytes)
            {
            }
        };

    } // namespace

    struct Kh02MovieBridge::Impl
    {
        FWeakObjectPtr pawn, world;
        Kh02MovieSourceContinuations continuations;
        Kh02MoviePlaybackState playback;
        using Phase = Kh02MoviePlaybackState::Phase;
        Kh02MovieSurface surface;
        Kh02MovieDecoderLease decoder;
        std::unique_ptr<Kh02MovieAudio> audio;
        std::string movie, source_movie, error, audio_warning;
        bool prewarmed{}, warm_hit{}, streaming{true}, story_movie{}, audible{};
        bool paused{}, matinee_audio{}, audio_output_disabled{};
        bool source_request_submitted{};
        std::string preferred_voice, embedded_voice;
        Clock::time_point paused_at{};
        Clock::time_point began{};
        Json history = Json::array();
        Json last_completion = Json::object();

        struct StreamState
        {
            bool available{}, natural_end{}, released{};
            int last_frame = -1, total_frames{}, published{}, dropped{};

            auto snapshot() const -> Json
            {
                if (!available)
                {
                    return Json::object();
                }
                return {{"transport", "shared_bgra_v1"}, {"last_frame", last_frame}, {"total_frames", total_frames},
                        {"published", published},        {"dropped", dropped},       {"natural_end", natural_end},
                        {"released", released}};
            }
        } stream_state;

        Kh02MovieFramePresenter presenter;

        ~Impl()
        {
            stop();
        }

        void record()
        {
            if (history.size() < history_event_limit)
            {
                history.push_back({{"phase", playback.name()},
                                   {"at_ms", GetTickCount64()},
                                   {"accepted", presenter.accepted_frames()},
                                   {"shown", presenter.shown_frame()}});
            }
        }

        void capture_failure(std::string_view reason) noexcept
        {
            // Operational error evidence belongs in player builds. Capture before
            // stop() cancels the request and releases the provider's shared state.
            try
            {
                auto failure = snapshot();
                failure["schema"] = "kh3ap.kh02_movie_failure.v1";
                failure["captured_at_utc"] = now_iso8601_utc();
                failure["process_id"] = GetCurrentProcessId();
                failure["error"] = reason;
                failure["source_movie"] = source_movie;
                // A preparation failure must not inherit the preceding movie's
                // retained decoder request identity and match its result file.
                failure["source_pid"] = source_request_submitted ? decoder.provider_pid() : 0;
                failure["control_request_id"] = source_request_submitted ? decoder.request_id() : "";
                failure["elapsed_ms"] =
                    began == Clock::time_point{}
                        ? 0
                        : std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - began).count();
                failure["world"] = world.Get() ? to_utf8(world.Get()->GetPathName()) : "";
                const auto companion = kh02_companion_health();
                failure["provider"] = {{"pid", companion.identity.source_pid},
                                       {"owner_pid", companion.identity.owner_pid},
                                       {"session_generation", companion.identity.session_generation},
                                       {"running", companion.running},
                                       {"ready", companion.ready}};
                if (const auto* frames = decoder.frames())
                {
                    const auto& header = frames->header();
                    failure["source_state"] = static_cast<int>(frames->state());
                    failure["shared"] = {{"last_frame", kh02movie::read(header.last_frame)},
                                         {"total_frames", kh02movie::read(header.total_frames)},
                                         {"published", kh02movie::read(header.published)},
                                         {"dropped", kh02movie::read(header.dropped)},
                                         {"released", kh02movie::read(header.released)},
                                         {"command", kh02movie::read(header.command)},
                                         {"audio_error", kh02movie::read(header.audio_error)},
                                         {"audio_channels", kh02movie::read(header.audio_channels)},
                                         {"audio_sample_rate", kh02movie::read(header.audio_sample_rate)},
                                         {"audio_published", kh02movie::read(header.audio_published)},
                                         {"audio_consumed", kh02movie::read(header.audio_consumed)}};
                }
                const auto text = failure.dump(-1, ' ', true);
                RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] KH02 movie failure {}\n"),
                                                      std::wstring(text.begin(), text.end()));
                std::array<wchar_t, 32768> executable{};
                const auto length =
                    GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
                if (length && length < executable.size())
                {
                    const auto path = std::filesystem::path(std::wstring(executable.data(), length)).parent_path() /
                                      L"kh02_movie_failure.json";
                    if (!write_text_file_atomic_verified(path, text))
                    {
                        RC::Output::send<RC::LogLevel::Warning>(
                            STR("[KH3APProbeCpp] KH02 movie failure sidecar write failed; "
                                "details retained in UE4SS log\n"));
                    }
                }
            }
            catch (...)
            {
                // Reporting must not interrupt the normal recovery path.
            }
        }

        template <class Operation> void audio_output(Operation&& operation)
        {
            try_kh02_audio_output(std::forward<Operation>(operation),
                                  [this](const char* warning)
                                  {
                                      audio_warning = warning;
                                      if (audio)
                                      {
                                          audio->stop();
                                          audio.reset();
                                      }
                                      audible = false;
                                      audio_output_disabled = true;
                                      record();
                                  });
        }

        void discard_muted_audio()
        {
            // The source still owns PCM production. Drain its bounded queue so
            // losing a local output device cannot itself stall that producer.
            // A producer error or malformed packet remains a transport failure.
            if (kh02movie::read(decoder.frames()->header().audio_error))
            {
                throw std::runtime_error("movie_audio_shared_failed");
            }
            for (uint32_t packet = 0; packet < kh02movie::audio_slot_count; ++packet)
            {
                if (!decoder.frames()->discard_audio())
                {
                    break;
                }
            }
        }

        void stop() noexcept
        {
            decoder.cancel_request();
            if (audio)
            {
                audio->stop();
            }
            decoder.release_mapping();
            decoder.close_provider();
            surface.stop();
            // A source graph may begin WaitForVideoFinish after EOF. Retain only
            // weak context identity for that completed slot; no movie resources.
            if (playback.phase != Phase::completed || !playback.finished)
            {
                pawn = {};
                world = {};
                continuations.release_source();
            }
            continuations.reset_waits();
            prewarmed = false;
            paused = false;
            presenter.clear();
            playback.stop();
        }

        void prepare(UObject* context, UObject* script, const std::wstring& name, bool event)
        {
            if (prewarmed && movie == to_utf8(name) && pawn.Get() == context && is_uobject_valid_fast(context) &&
                world.Get() == static_cast<AActor*>(context)->GetWorld() && is_uobject_valid_fast(script) && event &&
                (playback.phase == Phase::decoding || playback.phase == Phase::ready))
            {
                continuations.bind(script, !story_movie);
                prewarmed = false;
                warm_hit = true;
                record();
                return;
            }
            ++playback.generation;
            stop();
            audio.reset();
            error.clear();
            source_movie.clear();
            source_request_submitted = false;
            began = Clock::now();
            audio_warning.clear();
            audio_output_disabled = false;
            history = Json::array();
            stream_state = {};
            presenter.reset_request();
            continuations.reset_request();
            playback.reset_request();
            warm_hit = false;
            // Keep the attempted identity in diagnostics even when the audit rejects it.
            movie = to_utf8(name);
            std::replace(movie.begin(), movie.end(), '\\', '/');
            story_movie = false;
            preferred_voice = kh02_voice_language_code(current_kh02_voice_language());
            const auto request_plan = kh02_movie_request_plan(name, preferred_voice);
            source_movie = request_plan.file;
            const auto* story = request_plan.story;
            story_movie = story != nullptr;
            if (!is_uobject_valid_fast(context) || event && !is_uobject_valid_fast(script))
            {
                throw std::runtime_error("movie_bridge_context");
            }
            pawn = make_uobject_weak(context);
            world = make_uobject_weak(static_cast<AActor*>(context)->GetWorld());
            continuations.bind(script, event && !story_movie);
            const auto companion = kh02_companion_session();
            if (!companion.running || !companion.owned || !companion.executable_verified || !companion.input_isolated)
            {
                throw std::runtime_error("movie_bridge_companion:running=" + std::to_string(companion.running) +
                                         ":owned=" + std::to_string(companion.owned) +
                                         ":image_verified=" + std::to_string(companion.executable_verified) +
                                         ":input_known=" + std::to_string(companion.input_desktop_known) +
                                         ":input_matches=" + std::to_string(companion.input_desktop_matches) +
                                         ":input_isolated=" + std::to_string(companion.input_isolated));
            }
            decoder.open_provider(companion.identity.source_pid);
            surface.prepare(context);
            decoder.create_mapping();
            // Most scenes use localized Matinee audio. DW033/DW034 use embedded
            // dialogue and effects to follow the displayed movie clock. Their
            // authored BGM stays separate; overlapping native voices are muted.
            matinee_audio = story && story->matinee_audio;
            // The provider records the resolved language after installed-file fallback.
            embedded_voice = story && story->embedded_voice ? "provider_selected" : "";
            audible = story_movie && is_uobject_valid_fast(script) && !matinee_audio;
            if (audible)
            {
                // The companion's decoder clock precedes KH3's audio output.
                // Allocate a bounded presentation queue before playback starts.
                presenter.prepare_audio_queue(kh02movie::frame_bytes);
            }
            decoder.request_stream(companion.identity, request_plan.file, audible);
            source_request_submitted = true;
            playback.phase = Phase::decoding;
            began = Clock::now();
            record();
        }

        bool needs_tick() const
        {
            return playback.needs_tick();
        }

        void tick(UObject* context)
        {
            if (!needs_tick())
            {
                return;
            }
            if (!is_uobject_valid_fast(context) || pawn.Get() != context ||
                world.Get() != static_cast<AActor*>(context)->GetWorld())
            {
                stop();
                return;
            }
            decoder.check_provider();
            if (playback.phase == Phase::decoding && Clock::now() - began > preparation_timeout)
            {
                throw std::runtime_error("movie_bridge_prepare_timeout");
            }
            decoder.check_reply();
            const auto active_generation = playback.generation;
            const auto state = decoder.frames()->state();
            auto& info = decoder.frames()->header();
            if (state == kh02movie::State::failed || state == kh02movie::State::canceled)
            {
                throw std::runtime_error(state == kh02movie::State::failed ? "movie_bridge_source_failed"
                                                                           : "movie_bridge_source_canceled");
            }
            if (playback.phase == Phase::decoding && state == kh02movie::State::ready)
            {
                const auto total = kh02movie::read(info.total_frames);
                const auto numerator = kh02movie::read(info.fps_numerator);
                const auto denominator = kh02movie::read(info.fps_denominator);
                if (total < 1 || total > maximum_movie_frame_count || numerator < 1 || denominator < 1 ||
                    double(numerator) / denominator < minimum_movie_fps ||
                    double(numerator) / denominator > maximum_movie_fps)
                {
                    throw std::runtime_error("movie_bridge_shared_video_profile");
                }
                if (audible && !kh02movie::read(info.audio_channels) && !kh02movie::read(info.audio_sample_rate))
                {
                    audible = false;
                    presenter.discard_audio_queue();
                }
                if (audible)
                {
                    audio_output(
                        [&]
                        {
                            audio = std::make_unique<Kh02MovieAudio>(kh02movie::read(info.audio_channels),
                                                                     kh02movie::read(info.audio_sample_rate));
                        });
                }
                playback.phase = Phase::ready;
                record();
            }
            if (playback.phase == Phase::ready && continuations.ready_pending())
            {
                continuations.dispatch_ready();
                // A source callback may stop or replace this movie synchronously.
                if (!decoder.frames() || playback.phase != Phase::ready || playback.generation != active_generation)
                {
                    return;
                }
            }
            Call pause_state(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"), L"IsGamePaused");
            pause_state.object(L"WorldContextObject", context);
            pause_state.run();
            const bool should_pause = pause_state.boolean_result();
            if (playback.phase == Phase::ready && playback.resume_requested && !should_pause)
            {
                decoder.frames()->signal(kh02movie::Command::start);
                playback.phase = Phase::stream_playing;
                began = Clock::now();
                record();
            }
            if (playback.phase != Phase::stream_playing)
            {
                return;
            }
            if (paused != should_pause)
            {
                if (audio)
                {
                    audio_output([&] { audio->set_paused(should_pause); });
                }
                decoder.frames()->signal(should_pause ? kh02movie::Command::paused : kh02movie::Command::start);
                if (should_pause)
                {
                    paused_at = Clock::now();
                }
                else
                {
                    began += Clock::now() - paused_at;
                }
                paused = should_pause;
                record();
            }
            if (paused)
            {
                return;
            }
            if (Clock::now() - began > playback_timeout)
            {
                throw std::runtime_error("movie_bridge_playback_timeout");
            }
            if (audio)
            {
                audio_output([&] { audio->tick(*decoder.frames(), kh02_movie_volume(world.Get())); });
                if (audio && Clock::now() - began > std::chrono::seconds(2) && !kh02movie::read(info.audio_published))
                {
                    throw std::runtime_error("movie_audio_source_timeout");
                }
            }
            if (audio_output_disabled)
            {
                discard_muted_audio();
            }
            presenter.present(
                *decoder.frames(), audio != nullptr,
                [&]
                {
                    return audio->played_frames() * kh02movie::read(info.fps_numerator) /
                           (uint64_t(kh02movie::read(info.audio_sample_rate)) * kh02movie::read(info.fps_denominator));
                },
                [this](const auto& pixels) { surface.upload(pixels); }, [this] { surface.show(); },
                [] { return GetTickCount64(); });
            const auto latest = kh02movie::read(info.last_frame);
            stream_state.available = true;
            stream_state.last_frame = latest;
            stream_state.total_frames = kh02movie::read(info.total_frames);
            stream_state.published = kh02movie::read(info.published);
            stream_state.dropped = kh02movie::read(info.dropped);
            stream_state.natural_end = state == kh02movie::State::ended;
            stream_state.released = kh02movie::read(info.released) != 0;
            // Keep the final upload on screen across a presentation interval.
            // Only real decoder EOF and released audio may finish a source movie.
            if (state != kh02movie::State::ended || !kh02movie::read(info.released) ||
                !presenter.ready_to_finish(latest, GetTickCount64()) || audio && !audio->drained(*decoder.frames()))
            {
                return;
            }
            const auto completion = continuations.completion();
            const auto logical = movie;
            last_completion = {{"movie", movie},
                               {"shown", presenter.shown_frame()},
                               {"total_frames", stream_state.total_frames},
                               {"natural_end", stream_state.natural_end},
                               {"released", stream_state.released},
                               {"preferred_voice", preferred_voice},
                               {"effective_voice", matinee_audio ? preferred_voice : embedded_voice}};
            playback.complete();
            record();
            stop();
            completion.dispatch(logical,
                                [this, active_generation] { return playback.generation == active_generation; });
        }

        auto snapshot() const -> Json
        {
            return {{"phase", playback.name()},
                    {"error", error},
                    {"audio_warning", audio_warning},
                    {"audio_output_disabled", audio_output_disabled},
                    {"movie", movie},
                    {"source_pid", decoder.provider_pid()},
                    {"control_transport", "named_pipe_v1"},
                    {"control_request_id", decoder.request_id()},
                    {"control_pending", decoder.request_pending()},
                    {"requested", presenter.requested_frames()},
                    {"accepted", presenter.accepted_frames()},
                    {"shown", presenter.shown_frame()},
                    {"received", presenter.received_frame()},
                    {"ready", playback.phase == Phase::ready || playback.phase == Phase::stream_playing ||
                                  playback.phase == Phase::completed},
                    {"playing", playback.phase == Phase::stream_playing},
                    {"paused", paused},
                    {"preferred_voice", preferred_voice},
                    {"effective_voice", matinee_audio ? preferred_voice : embedded_voice},
                    {"voice_fallback", audible ? Json(nullptr) : Json(false)},
                    {"audio_owner", matinee_audio ? "localized_matinee"
                                    : audible     ? "movie_pcm"
                                                  : "silent"},
                    {"finished", playback.finished},
                    {"completed_waits", continuations.completed_wait_count()},
                    {"terminal_finish_waits", continuations.terminal_wait_count()},
                    {"ready_callback_sent", continuations.ready_callback_sent()},
                    {"original_finish_event", continuations.has_gear_finish()},
                    {"prewarmed", prewarmed},
                    {"warm_hit", warm_hit},
                    {"events", history},
                    {"last_completion", last_completion},
                    {"streaming", streaming},
                    {"stream_state", stream_state.snapshot()},
                    {"audio", audio ? audio->snapshot() : Json::object()},
                    {"disk_cache", false},
                    {"transport", "shared_bgra_v1"},
                    {"http_listener", false},
                    {"shared_bytes", kh02movie::mapping_bytes},
                    {"presentation", "native_scaleform"},
                    {"presentation_priority", Kh02MovieSurface::priority},
                    {"texture_count", is_uobject_valid_fast(surface.texture()) ? 2 : 0}};
        }
    };

    Kh02MovieBridge::Kh02MovieBridge() : impl(std::make_unique<Impl>()) {}

    Kh02MovieBridge::~Kh02MovieBridge() = default;

    void Kh02MovieBridge::prepare(UObject* pawn, UObject* script, const std::wstring& movie, bool finish)
    {
        try
        {
            impl->prepare(pawn, script, movie, finish);
        }
        catch (const std::exception& error)
        {
            impl->capture_failure(error.what());
            impl->stop();
            impl->error = error.what();
            impl->playback.phase = Kh02MoviePlaybackState::Phase::error;
            impl->record();
            throw;
        }
    }

    void Kh02MovieBridge::prewarm(UObject* pawn, const std::wstring& movie)
    {
        try
        {
            impl->prepare(pawn, nullptr, movie, false);
            impl->prewarmed = true;
        }
        catch (const std::exception& error)
        {
            impl->capture_failure(error.what());
            impl->stop();
            throw;
        }
    }

    void Kh02MovieBridge::wait(UObject* script, int linkage, const std::wstring& function)
    {
        impl->continuations.wait(script, linkage, function, impl->story_movie);
    }

    void Kh02MovieBridge::wait_finish(UObject* script, int linkage, const std::wstring& function)
    {
        impl->continuations.wait_finish(script, linkage, function, impl->story_movie, impl->playback.completed(),
                                        impl->pawn.Get(), impl->world.Get());
    }

    void Kh02MovieBridge::resume()
    {
        impl->playback.resume();
    }

    void Kh02MovieBridge::stop()
    {
        impl->stop();
    }

    void Kh02MovieBridge::retire_session()
    {
        ++impl->playback.generation;
        impl->stop();
        impl->pawn = {};
        impl->world = {};
        impl->continuations.retire();
        impl->playback.retire();
        impl->story_movie = false;
        impl->last_completion = Json::object();
        impl->stream_state = {};
        impl->movie.clear();
        impl->error.clear();
    }

    void Kh02MovieBridge::tick(UObject* pawn)
    {
        try
        {
            impl->tick(pawn);
        }
        catch (const std::exception& e)
        {
            impl->capture_failure(e.what());
            impl->stop();
            impl->error = e.what();
            impl->playback.phase = Kh02MoviePlaybackState::Phase::error;
            impl->record();
        }
    }

    auto Kh02MovieBridge::snapshot() const -> Json
    {
        return impl->snapshot();
    }

    auto Kh02MovieBridge::needs_tick() const -> bool
    {
        return impl->needs_tick();
    }

    auto Kh02MovieBridge::playing() const -> bool
    {
        return impl->playback.phase == Kh02MoviePlaybackState::Phase::stream_playing;
    }

    auto Kh02MovieBridge::presentation_seconds() const -> std::optional<float>
    {
        if (!playing() || !impl->decoder.frames() || impl->presenter.shown_frame() < 0)
        {
            return std::nullopt;
        }
        const auto& header = impl->decoder.frames()->header();
        const auto numerator = kh02movie::read(header.fps_numerator);
        const auto denominator = kh02movie::read(header.fps_denominator);
        if (numerator <= 0 || denominator <= 0)
        {
            return std::nullopt;
        }
        return float(double(impl->presenter.shown_frame()) * denominator / numerator);
    }

    auto Kh02MovieBridge::completed_movie(std::string_view logical) const -> bool
    {
        return impl->last_completion.value("movie", std::string{}) == logical &&
               impl->last_completion.value("natural_end", false) && impl->last_completion.value("released", false);
    }

    auto Kh02MovieBridge::error() const -> const std::string&
    {
        return impl->error;
    }
} // namespace kh3ap
