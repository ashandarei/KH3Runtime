#include "kh3ap/features/world/portals/portal_auto_spawn_state.h"
#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/player/objects/map_actor_lifecycle_service.h"

#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_service.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/player/context/context_state.h"
#include "kh3ap/features/player/context/context_subsystem_state.h"
#include "kh3ap/features/player/context/player_context_service.h"
#include "kh3ap/features/world/portals/portal_spawn_helpers.h"
#include "kh3ap/features/story/save_points/save_point_spawn_helpers.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <string>
#include <utility>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

namespace kh3ap
{
    namespace
    {
        using ::kh3ap::settings::s_actor_get_location_path;
        using ::kh3ap::settings::s_bridge_protocol_name;
        using ::kh3ap::settings::s_bridge_protocol_version;
        using ::kh3ap::settings::s_build_stamp;
        using ::kh3ap::settings::s_bx_portal_blueprint_path;
        using ::kh3ap::settings::s_bx_portal_class_path;
        using ::kh3ap::settings::s_create_save_game_object_path;
        using ::kh3ap::settings::s_death_mirror_blueprint_path;
        using ::kh3ap::settings::s_death_mirror_class_path;
        using ::kh3ap::settings::s_debug_logging;
        using ::kh3ap::settings::s_enable_runtime_bx02_portal_auto_spawn;
        using ::kh3ap::settings::s_enable_runtime_fz02_save_point_auto_spawn;
        using ::kh3ap::settings::s_enable_runtime_he02_death_mirror_auto_spawn;
        using ::kh3ap::settings::s_enable_runtime_he02_save_point_auto_spawn;
        using ::kh3ap::settings::s_enable_runtime_hot_path_profiling;
        using ::kh3ap::settings::s_enable_runtime_portal_spawn_request;
        using ::kh3ap::settings::s_enable_runtime_save_point_lifetime_deep_samples;
        using ::kh3ap::settings::s_gameplay_statics_default_object_path;
        using ::kh3ap::settings::s_get_game_flag_path;
        using ::kh3ap::settings::s_goa_save_point_data_blueprint_path;
        using ::kh3ap::settings::s_goa_save_point_data_class_path;
        using ::kh3ap::settings::s_load_game_from_slot_path;
        using ::kh3ap::settings::s_runtime_bx02_portal_auto_spawn_map_path;
        using ::kh3ap::settings::s_runtime_bx02_portal_auto_spawn_retry_interval;
        using ::kh3ap::settings::s_runtime_bx02_portal_auto_spawn_stable_delay;
        using ::kh3ap::settings::s_runtime_bx02_portal_auto_spawn_verification_delay;
        using ::kh3ap::settings::s_runtime_bx02_portal_auto_spawn_x;
        using ::kh3ap::settings::s_runtime_bx02_portal_auto_spawn_y;
        using ::kh3ap::settings::s_runtime_bx02_portal_auto_spawn_z;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_disp_map_name;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_level_path_property;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_map_path;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_player_start_property;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_player_start_tag;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_retry_interval;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_slot_name;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_stable_delay;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_x;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_y;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_z;
        using ::kh3ap::settings::s_runtime_he02_death_mirror_auto_spawn_map_path;
        using ::kh3ap::settings::s_runtime_he02_death_mirror_auto_spawn_retry_interval;
        using ::kh3ap::settings::s_runtime_he02_death_mirror_auto_spawn_stable_delay;
        using ::kh3ap::settings::s_runtime_he02_death_mirror_auto_spawn_verification_delay;
        using ::kh3ap::settings::s_runtime_he02_death_mirror_auto_spawn_x;
        using ::kh3ap::settings::s_runtime_he02_death_mirror_auto_spawn_y;
        using ::kh3ap::settings::s_runtime_he02_death_mirror_auto_spawn_z;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_disp_map_name;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_level_path_property;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_map_path;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_player_start_property;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_player_start_tag;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_retry_interval;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_slot_name;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_stable_delay;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_x;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_y;
        using ::kh3ap::settings::s_runtime_he02_save_point_auto_spawn_z;
        using ::kh3ap::settings::s_runtime_portal_spawn_events_filename;
        using ::kh3ap::settings::s_runtime_portal_spawn_request_filename;
        using ::kh3ap::settings::s_runtime_portal_spawn_result_filename;
        using ::kh3ap::settings::s_runtime_save_point_default_my_map_marker_path;
        using ::kh3ap::settings::s_runtime_save_point_default_my_reactor_path;
        using ::kh3ap::settings::s_runtime_save_point_default_my_recover_path;
        using ::kh3ap::settings::s_runtime_save_point_default_player_start_tag;
        using ::kh3ap::settings::s_runtime_save_point_spawn_events_filename;
        using ::kh3ap::settings::s_runtime_save_point_spawn_request_filename;
        using ::kh3ap::settings::s_runtime_save_point_spawn_result_filename;
        using ::kh3ap::settings::s_save_game_to_slot_path;
        using ::kh3ap::settings::s_save_point_blueprint_path;
        using ::kh3ap::settings::s_save_point_class_path;
        using ::kh3ap::settings::s_tres_game_blueprint_library_default_object_path;

        auto make_save_point_deps(
                RuntimeState& runtime_state,
                const std::optional<std::filesystem::path>& bridge_dir)
                -> RuntimeSavePointSpawnDeps
        {
            return RuntimeSavePointSpawnDeps{
                    runtime_state,
                    bridge_dir,
                    s_runtime_save_point_spawn_request_filename,
                    s_runtime_save_point_spawn_result_filename,
                    s_runtime_save_point_spawn_events_filename,
                    s_bridge_protocol_name,
                    s_bridge_protocol_version,
                    s_build_stamp,
                    s_save_point_class_path,
                    s_save_point_blueprint_path,
                    s_actor_get_location_path,
                    s_gameplay_statics_default_object_path,
                    s_load_game_from_slot_path,
                    s_create_save_game_object_path,
                    s_save_game_to_slot_path,
                    s_goa_save_point_data_class_path,
                    s_goa_save_point_data_blueprint_path,
                    s_runtime_save_point_default_player_start_tag,
                    s_runtime_save_point_default_my_reactor_path,
                    s_runtime_save_point_default_my_recover_path,
                    s_runtime_save_point_default_my_map_marker_path,
                    s_enable_runtime_save_point_lifetime_deep_samples,
                    s_debug_logging};
        }

        auto make_portal_deps(
                RuntimeState& runtime_state,
                const std::optional<std::filesystem::path>& bridge_dir,
                std::wstring_view class_path,
                std::wstring_view blueprint_path,
                RuntimePortalClassCache& class_cache) -> RuntimePortalSpawnDeps
        {
            return RuntimePortalSpawnDeps{
                    runtime_state,
                    bridge_dir,
                    s_runtime_portal_spawn_request_filename,
                    s_runtime_portal_spawn_result_filename,
                    s_runtime_portal_spawn_events_filename,
                    s_bridge_protocol_name,
                    s_bridge_protocol_version,
                    s_build_stamp,
                    class_path,
                    blueprint_path,
                    s_actor_get_location_path,
                    s_debug_logging,
                    &class_cache};
        }
    }

    struct RuntimeMapActorLifecycleService::Impl
    {
        explicit Impl(RuntimeMapActorLifecycleConfig config)
            : config(std::move(config))
        {
        }

        auto cache_portal_class_object(
                RuntimePortalClassCache& class_cache,
                RuntimeAutoPortalSpawnState& state,
                RC::Unreal::UObject* object,
                std::wstring_view expected_class_path,
                std::wstring_view source_name) -> bool
        {
            RC::Unreal::UClass* const class_class =
                    RC::Unreal::UClass::StaticClass();
            if (!kh3ap::is_uobject_a_fast(object, class_class))
            {
                return false;
            }

            std::wstring full_name{};
            if (!kh3ap::try_get_uobject_full_name_fast(object, full_name) ||
                full_name.find(expected_class_path) == std::wstring::npos)
            {
                return false;
            }

            auto* const portal_class = static_cast<RC::Unreal::UClass*>(object);
            const bool newly_retained = class_cache.retain(portal_class);
            if (class_cache.retained_class() != portal_class)
            {
                return false;
            }

            // A completion that arrives just after the bounded retry window can
            // safely re-arm the current map entry because the class is now held.
            if (state.class_load_failed_closed)
            {
                state.spawn_attempted = false;
                state.asset_load_requested = true;
                state.playable_asset_load_requested = true;
                state.last_attempt_at = {};
                state.verification_complete = false;
                state.class_load_failed_closed = false;
            }

            if (newly_retained)
            {
                pending = true;
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] RuntimePortalClassCache source={} status=rooted class={}\n"),
                        source_name,
                        full_name);
            }
            return true;
        }

        auto cache_bx_portal_class(RC::Unreal::UObject* object) -> bool
        {
            return cache_portal_class_object(
                    bx02_portal_class_cache,
                    bx02_portal_state,
                    object,
                    s_bx_portal_class_path,
                    STR("BX_Portal_C"));
        }

        auto cache_he02_death_mirror_class(RC::Unreal::UObject* object) -> bool
        {
            return cache_portal_class_object(
                    he02_death_mirror_class_cache,
                    he02_death_mirror_state,
                    object,
                    s_death_mirror_class_path,
                    STR("A_GoA_DeathMirror_C"));
        }

        auto reset() -> void
        {
            pending = true;
            he02_save_point_state = {};
            he02_save_point_story_skip_logged = false;
            fz02_save_point_state = {};
            bx02_portal_state = {};
            he02_death_mirror_state = {};
            he02_death_mirror_entry_context_key.clear();
            he02_death_mirror_entry_spawn_allowed.reset();
        }

        auto he02_save_point_needed() -> bool
        {
            int32_t gameflow_he{};
            if (!try_call_get_game_flag(
                        STR("gameflow_HE"),
                        gameflow_he,
                        config.runtime_state,
                        s_get_game_flag_path,
                        s_tres_game_blueprint_library_default_object_path,
                        s_debug_logging))
            {
                return false;
            }
            const bool needed = runtime_he02_save_point_needed(gameflow_he);
            if (!needed && !he02_save_point_story_skip_logged)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] RuntimeMapSavePointAutoSpawn source=auto_he02 status=skipped reason=story_cutoff gameflow_HE={} cutoff=300\n"),
                        gameflow_he);
            }
            he02_save_point_story_skip_logged = !needed;
            return needed;
        }

        auto process(
                const std::optional<std::filesystem::path>& bridge_dir,
                std::chrono::steady_clock::time_point now,
                const PhaseMarker& mark_phase) -> void
        {
            if constexpr (s_enable_runtime_he02_save_point_auto_spawn)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:runtime_he02_save_point_auto_spawn")};
                auto deps = make_save_point_deps(config.runtime_state, bridge_dir);
                deps.auto_spawn_allowed = [this]() { return he02_save_point_needed(); };
                process_runtime_map_save_point_auto_spawn(
                        he02_save_point_state,
                        deps,
                        now,
                        STR("auto_he02"),
                        STR("auto-he02-save-point"),
                        s_runtime_he02_save_point_auto_spawn_map_path,
                        s_runtime_he02_save_point_auto_spawn_player_start_tag,
                        s_runtime_he02_save_point_auto_spawn_disp_map_name,
                        s_runtime_he02_save_point_auto_spawn_x,
                        s_runtime_he02_save_point_auto_spawn_y,
                        s_runtime_he02_save_point_auto_spawn_z,
                        s_runtime_he02_save_point_auto_spawn_stable_delay,
                        s_runtime_he02_save_point_auto_spawn_retry_interval,
                        s_runtime_he02_save_point_auto_spawn_slot_name,
                        s_runtime_he02_save_point_auto_spawn_level_path_property,
                        s_runtime_he02_save_point_auto_spawn_player_start_property);
            }
            mark_phase(STR("runtime_file_poll_he02_auto_spawn"));

            if constexpr (s_enable_runtime_fz02_save_point_auto_spawn)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:runtime_fz02_save_point_auto_spawn")};
                process_runtime_map_save_point_auto_spawn(
                        fz02_save_point_state,
                        make_save_point_deps(config.runtime_state, bridge_dir),
                        now,
                        STR("auto_fz02"),
                        STR("auto-fz02-save-point"),
                        s_runtime_fz02_save_point_auto_spawn_map_path,
                        s_runtime_fz02_save_point_auto_spawn_player_start_tag,
                        s_runtime_fz02_save_point_auto_spawn_disp_map_name,
                        s_runtime_fz02_save_point_auto_spawn_x,
                        s_runtime_fz02_save_point_auto_spawn_y,
                        s_runtime_fz02_save_point_auto_spawn_z,
                        s_runtime_fz02_save_point_auto_spawn_stable_delay,
                        s_runtime_fz02_save_point_auto_spawn_retry_interval,
                        s_runtime_fz02_save_point_auto_spawn_slot_name,
                        s_runtime_fz02_save_point_auto_spawn_level_path_property,
                        s_runtime_fz02_save_point_auto_spawn_player_start_property);
            }
            mark_phase(STR("runtime_file_poll_fz02_auto_spawn"));

            if constexpr (s_enable_runtime_portal_spawn_request)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:runtime_portal_spawn")};
                process_pending_runtime_portal_spawn(make_portal_deps(
                        config.runtime_state,
                        bridge_dir,
                        s_bx_portal_class_path,
                        s_bx_portal_blueprint_path,
                        bx02_portal_class_cache));
            }
            mark_phase(STR("runtime_file_poll_portal_request"));

            if constexpr (s_enable_runtime_bx02_portal_auto_spawn)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:runtime_bx02_portal_auto_spawn")};
                process_runtime_map_portal_auto_spawn(
                        bx02_portal_state,
                        make_portal_deps(
                                config.runtime_state,
                                bridge_dir,
                                s_bx_portal_class_path,
                                s_bx_portal_blueprint_path,
                                bx02_portal_class_cache),
                        now,
                        STR("auto_bx02_portal"),
                        STR("auto-bx02-portal"),
                        s_runtime_bx02_portal_auto_spawn_map_path,
                        s_runtime_bx02_portal_auto_spawn_x,
                        s_runtime_bx02_portal_auto_spawn_y,
                        s_runtime_bx02_portal_auto_spawn_z,
                        s_runtime_bx02_portal_auto_spawn_stable_delay,
                        s_runtime_bx02_portal_auto_spawn_retry_interval,
                        s_runtime_bx02_portal_auto_spawn_verification_delay,
                        config.player_context.map_ready_context_key());
            }
            mark_phase(STR("runtime_file_poll_bx02_auto_spawn"));

            if constexpr (s_enable_runtime_he02_death_mirror_auto_spawn)
            {
                RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                        STR("on_update:runtime_he02_death_mirror_auto_spawn")};

                bool eligible = false;
                std::wstring ineligibility_reason{STR("target_map_context_unavailable")};
                const auto context_snapshot = config.runtime_state
                                                      .context_subsystem_state()
                                                      .context_state()
                                                      .current_snapshot();
                const bool in_target_map =
                        context_snapshot.has_value() &&
                        context_snapshot->hook == STR("RuntimePlayablePawnContext") &&
                        (context_snapshot->world + STR(" ") + context_snapshot->outer +
                         STR(" ") + context_snapshot->object)
                                        .find(s_runtime_he02_death_mirror_auto_spawn_map_path) !=
                                std::wstring::npos;
                if (in_target_map)
                {
                    const std::wstring context_key =
                            context_snapshot->world + STR("|") + context_snapshot->object;
                    if (he02_death_mirror_entry_context_key != context_key)
                    {
                        he02_death_mirror_entry_context_key = context_key;
                        he02_death_mirror_entry_spawn_allowed.reset();
                    }

                    if (!he02_death_mirror_entry_spawn_allowed.has_value())
                    {
                        bool wall_run_owned = false;
                        if (config.controller_abilities.try_read_unlock_owned(
                                    STR("wall_run"),
                                    &wall_run_owned))
                        {
                            he02_death_mirror_entry_spawn_allowed =
                                    !wall_run_owned;
                        }
                    }

                    if (he02_death_mirror_entry_spawn_allowed.has_value())
                    {
                        eligible = *he02_death_mirror_entry_spawn_allowed;
                        ineligibility_reason = eligible
                                ? std::wstring{}
                                : std::wstring{STR("wall_run_owned_at_map_entry")};
                    }
                    else
                    {
                        ineligibility_reason = STR("wall_run_ability_state_unavailable");
                    }
                }
                else
                {
                    he02_death_mirror_entry_context_key.clear();
                    he02_death_mirror_entry_spawn_allowed.reset();
                }

                process_runtime_map_portal_auto_spawn(
                        he02_death_mirror_state,
                        make_portal_deps(
                                config.runtime_state,
                                bridge_dir,
                                s_death_mirror_class_path,
                                s_death_mirror_blueprint_path,
                                he02_death_mirror_class_cache),
                        now,
                        STR("auto_he02_death_mirror"),
                        STR("auto-he02-death-mirror"),
                        s_runtime_he02_death_mirror_auto_spawn_map_path,
                        s_runtime_he02_death_mirror_auto_spawn_x,
                        s_runtime_he02_death_mirror_auto_spawn_y,
                        s_runtime_he02_death_mirror_auto_spawn_z,
                        s_runtime_he02_death_mirror_auto_spawn_stable_delay,
                        s_runtime_he02_death_mirror_auto_spawn_retry_interval,
                        s_runtime_he02_death_mirror_auto_spawn_verification_delay,
                        config.player_context.map_ready_context_key(),
                        eligible,
                        ineligibility_reason);
            }
            mark_phase(STR("runtime_file_poll_he02_death_mirror_auto_spawn"));
            const auto snapshot = config.runtime_state.context_subsystem_state().context_state().current_snapshot();
            // Lifecycle reset arms startup work. Only unfinished work in the
            // current target map keeps a timer after a playable context arrives.
            if (snapshot && !snapshot->world.empty()) {
                const auto map = snapshot->world + STR(" ") + snapshot->outer;
                pending =
                    (map.find(s_runtime_he02_save_point_auto_spawn_map_path)!=std::wstring::npos &&
                     ((s_enable_runtime_he02_save_point_auto_spawn && !he02_save_point_story_skip_logged && !he02_save_point_state.spawn_attempted && !he02_save_point_state.terminal_failure) ||
                      (s_enable_runtime_he02_death_mirror_auto_spawn && !he02_death_mirror_state.verification_complete && !he02_death_mirror_state.class_load_failed_closed &&
                       he02_death_mirror_entry_spawn_allowed!=false))) ||
                    (map.find(s_runtime_fz02_save_point_auto_spawn_map_path)!=std::wstring::npos &&
                     s_enable_runtime_fz02_save_point_auto_spawn && !fz02_save_point_state.spawn_attempted && !fz02_save_point_state.terminal_failure) ||
                    (map.find(s_runtime_bx02_portal_auto_spawn_map_path)!=std::wstring::npos &&
                     s_enable_runtime_bx02_portal_auto_spawn && !bx02_portal_state.verification_complete && !bx02_portal_state.class_load_failed_closed);
            }
        }

        RuntimeMapActorLifecycleConfig config;
        bool pending{true};
        RuntimeAutoSavePointSpawnState he02_save_point_state{};
        bool he02_save_point_story_skip_logged{};
        RuntimeAutoSavePointSpawnState fz02_save_point_state{};
        RuntimePortalClassCache bx02_portal_class_cache{};
        RuntimePortalClassCache he02_death_mirror_class_cache{};
        RuntimeAutoPortalSpawnState bx02_portal_state{};
        RuntimeAutoPortalSpawnState he02_death_mirror_state{};
        std::wstring he02_death_mirror_entry_context_key{};
        std::optional<bool> he02_death_mirror_entry_spawn_allowed{};
    };

    RuntimeMapActorLifecycleService::RuntimeMapActorLifecycleService(
            RuntimeMapActorLifecycleConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeMapActorLifecycleService::~RuntimeMapActorLifecycleService() = default;
    RuntimeMapActorLifecycleService::RuntimeMapActorLifecycleService(
            RuntimeMapActorLifecycleService&&) noexcept = default;
    auto RuntimeMapActorLifecycleService::operator=(
            RuntimeMapActorLifecycleService&&) noexcept
            -> RuntimeMapActorLifecycleService& = default;

    auto RuntimeMapActorLifecycleService::reset() -> void
    {
        impl_->reset();
    }

    auto RuntimeMapActorLifecycleService::has_pending_work() const -> bool
    {
        return impl_->pending;
    }

    auto RuntimeMapActorLifecycleService::notify_context_ready() -> void
    {
        impl_->pending = true;
    }

    auto RuntimeMapActorLifecycleService::cache_bx_portal_class(
            RC::Unreal::UObject* object) -> bool
    {
        return impl_->cache_bx_portal_class(object);
    }

    auto RuntimeMapActorLifecycleService::cache_he02_death_mirror_class(
            RC::Unreal::UObject* object) -> bool
    {
        return impl_->cache_he02_death_mirror_class(object);
    }

    auto RuntimeMapActorLifecycleService::process(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::chrono::steady_clock::time_point now,
            const PhaseMarker& mark_phase) -> void
    {
        impl_->process(bridge_dir, now, mark_phase);
    }
}
