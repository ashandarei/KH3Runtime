#include "kh3ap/features/music/events/music_update_events.h"
#include "kh3ap/features/player/context/player_reacquire_service.h"

#include "kh3ap/features/grants/ap_ignored_map.h"
#include "kh3ap/features/player/context/context_state.h"
#include "kh3ap/features/player/context/context_subsystem_state.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/grants/delivery/item_grant_poll_state.h"
#include "kh3ap/features/player/context/player_reacquire_state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/weak_object_resolver.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    struct RuntimePlayerReacquireService::Impl
    {
        explicit Impl(RuntimePlayerReacquireConfig value)
            : config(std::move(value))
        {
        }

        auto reset_playable_publish() -> void
        {
            config.runtime_state.context_subsystem_state()
                    .context_state()
                    .reset_playable_publish();
        }

        auto enter_ignored_map(
                std::wstring_view observed_context,
                std::wstring_view source) -> void
        {
            const bool changed = state.enter_ignored_map(observed_context);
            if (config.reset_battle_level_lifecycle)
            {
                config.reset_battle_level_lifecycle();
            }
            if (config.clear_special_battle_pending)
            {
                config.clear_special_battle_pending();
            }
            config.runtime_state.grant_state().item_grant_poll_state().disarm();
            config.runtime_state.grant_state().pawn_state().reset();
            reset_playable_publish();
            if (config.reset_map_lifecycle)
            {
                config.reset_map_lifecycle();
            }
            if (changed)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] AP runtime ignored map entered map=yt_50 source={} context={}\n"),
                        source,
                        truncate_for_log(observed_context, 260));
            }
        }

        auto leave_ignored_map_from_validated_pawn(
                std::wstring_view pawn_name,
                std::wstring_view source) -> void
        {
            if (!state.leave_ignored_map_from_validated_pawn(pawn_name))
            {
                return;
            }

            request_music_update(MusicUpdate::Context);
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] AP runtime ignored map exited map=yt_50 source={} validated_pawn={}\n"),
                    source,
                    truncate_for_log(pawn_name, 260));
        }

        auto arm(UObject* controller, std::wstring_view source) -> void
        {
            if (!controller ||
                !kh3ap::is_uobject_valid_fast(controller) ||
                !config.is_player_controller ||
                !config.is_player_controller(controller))
            {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            const std::wstring controller_name =
                    format_object_debug_name(controller);
            if (is_runtime_ap_ignored_map_context(controller_name))
            {
                enter_ignored_map(controller_name, source);
                return;
            }
            config.runtime_state.grant_state().pawn_state().update_controller(
                    weak_uobject_resolver(controller),
                    controller_name);
            config.runtime_state.grant_state().pawn_state().clear_candidate();
            reset_playable_publish();
            state.arm(now);
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] SoraControllerReacquire armed source={} controller={} interval_ms={}\n"),
                    source,
                    controller_name,
                    static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                    config.reacquire_interval)
                                    .count()));
        }

        auto disarm(std::wstring_view source) -> void
        {
            if (!state.disarm())
            {
                return;
            }
            const std::wstring& pawn_name =
                    config.runtime_state.grant_state().pawn_state().name();
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] SoraControllerReacquire disarmed source={} pawn={}\n"),
                    source,
                    pawn_name.empty()
                            ? std::wstring{STR("<none>")}
                            : pawn_name);
        }

        auto process(std::chrono::steady_clock::time_point now) -> void
        {
            if (state.ignored_map_active())
            {
                state.suppress();
                return;
            }
            if (!state.reacquire_armed())
            {
                return;
            }

            // A successful cache from any runtime hook completes reacquisition
            // for this map visit. Trust the cache here; validating it would
            // reintroduce the expensive UObject scan this fallback avoids.
            auto& pawn_state = config.runtime_state.grant_state().pawn_state();
            if (pawn_state.object() && pawn_state.candidate_trusted())
            {
                if (config.sync_battle_level_after_reacquire)
                {
                    config.sync_battle_level_after_reacquire(
                            STR("special_battle_pawn_reacquired_cached"));
                }
                disarm(STR("cached_pawn_already_available"));
                return;
            }
            if (!state.should_attempt(now, config.reacquire_interval))
            {
                return;
            }

            UObject* const controller = pawn_state.controller_object();
            const std::wstring controller_name = pawn_state.controller_name();
            if (!controller || controller_name.empty())
            {
                state.stop_for_invalid_controller();
                pawn_state.reset();
                reset_playable_publish();
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] SoraControllerReacquire stopped reason=invalid_controller controller={}\n"),
                        controller_name);
                return;
            }

            const bool cached = config.cache_pawn_from_controller &&
                    config.cache_pawn_from_controller(
                            controller,
                            STR("SoraControllerReacquire:cached_controller"),
                            controller_name,
                            false);
            if (cached)
            {
                if (config.sync_battle_level_after_reacquire)
                {
                    config.sync_battle_level_after_reacquire(
                            STR("special_battle_pawn_reacquired_controller"));
                }
                disarm(STR("found_sora"));
                return;
            }

            pawn_state.clear_candidate();
            reset_playable_publish();
            const std::wstring runtime_context = config.runtime_context_resolver
                    ? config.runtime_context_resolver()
                    : std::wstring{};
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] SoraControllerReacquire waiting controller={} pawn={} | {}\n"),
                    controller_name,
                    pawn_state.name().empty()
                            ? std::wstring{STR("<none>")}
                            : pawn_state.name(),
                    runtime_context);
        }

        RuntimePlayerReacquireConfig config;
        RuntimePlayerReacquireState state{};
    };

    RuntimePlayerReacquireService::RuntimePlayerReacquireService(
            RuntimePlayerReacquireConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimePlayerReacquireService::~RuntimePlayerReacquireService() = default;
    RuntimePlayerReacquireService::RuntimePlayerReacquireService(
            RuntimePlayerReacquireService&&) noexcept = default;
    auto RuntimePlayerReacquireService::operator=(
            RuntimePlayerReacquireService&&) noexcept
            -> RuntimePlayerReacquireService& = default;

    auto RuntimePlayerReacquireService::enter_ignored_map(
            std::wstring_view observed_context,
            std::wstring_view source) -> void
    {
        impl_->enter_ignored_map(observed_context, source);
    }

    auto RuntimePlayerReacquireService::leave_ignored_map_from_validated_pawn(
            std::wstring_view pawn_name,
            std::wstring_view source) -> void
    {
        impl_->leave_ignored_map_from_validated_pawn(pawn_name, source);
    }

    auto RuntimePlayerReacquireService::arm(
            UObject* controller,
            std::wstring_view source) -> void
    {
        impl_->arm(controller, source);
    }

    auto RuntimePlayerReacquireService::disarm(
            std::wstring_view source) -> void
    {
        impl_->disarm(source);
    }

    auto RuntimePlayerReacquireService::suppress() -> void
    {
        impl_->state.suppress();
    }

    auto RuntimePlayerReacquireService::process(
            std::chrono::steady_clock::time_point now) -> void
    {
        impl_->process(now);
    }

    auto RuntimePlayerReacquireService::ignored_map_active() const -> bool
    {
        return impl_->state.ignored_map_active();
    }
}
