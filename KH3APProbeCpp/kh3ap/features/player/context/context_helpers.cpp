#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/player/context/context_helpers.h"

#include "kh3ap/features/grants/ap_ignored_map.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/features/player/context/context_resolver.h"
#include "kh3ap/features/player/context/context_state.h"
#include "kh3ap/features/player/context/context_store.h"
#include "kh3ap/features/grants/delivery/item_grant_poll_state.h"
#include "kh3ap/features/player/objects/object_cache.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>

#include <format>
#include <optional>
#include <string>

namespace kh3ap
{
    namespace
    {
        auto snapshot_context_text(const RuntimeContextSnapshot& snapshot) -> std::wstring
        {
            return snapshot.world + STR(" ") + snapshot.outer + STR(" ") + snapshot.object;
        }

        auto is_caribbean_context(const RuntimeContextSnapshot& snapshot) -> bool
        {
            const std::wstring context_text = snapshot_context_text(snapshot);
            return context_text.find(STR("/Game/Levels/ca/")) != std::wstring::npos;
        }

        auto is_corona_enemy_context(const RuntimeContextSnapshot& snapshot) -> bool
        {
            const std::wstring context_text = snapshot_context_text(snapshot);
            return context_text.find(STR("/Game/Levels/ra/")) != std::wstring::npos &&
                   context_text.find(STR("_enemy")) != std::wstring::npos;
        }

        auto is_monstropolis_lobby_unversed_context(const RuntimeContextSnapshot& snapshot) -> bool
        {
            return snapshot_context_text(snapshot).find(
                           STR("/Game/Levels/mi/mi_01/umap/mi_01_enemy_01")) != std::wstring::npos;
        }

        auto is_keyblade_graveyard_org_fight_context(const RuntimeContextSnapshot& snapshot) -> bool
        {
            const std::wstring context_text = snapshot_context_text(snapshot);
            if (context_text.find(STR("/Game/Levels/kg/kg_02/")) == std::wstring::npos)
            {
                return false;
            }

            return context_text.find(STR("kg_02_enemy_Area_B")) != std::wstring::npos ||
                   context_text.find(STR("kg_02_enemy_Area_C")) != std::wstring::npos ||
                   context_text.find(STR("kg_02_enemy_Area_D")) != std::wstring::npos;
        }

        auto is_remind_luxord_marluxia_larxene_context(const RuntimeContextSnapshot& snapshot) -> bool
        {
            return snapshot_context_text(snapshot).find(
                           STR("/Game/Levels/kg_DLC/kg_05/umap/kg_05_enemy_Area_B")) != std::wstring::npos;
        }

        auto is_remind_young_xehanort_ansem_xemnas_context(const RuntimeContextSnapshot& snapshot) -> bool
        {
            return snapshot_context_text(snapshot).find(
                           STR("/Game/Levels/kg_DLC/kg_05/umap/kg_05_enemy_Area_E")) != std::wstring::npos;
        }

    }

    auto read_runtime_context_snapshot(const std::optional<std::filesystem::path>& bridge_dir) -> std::optional<RuntimeContextSnapshot>
    {
        if (!bridge_dir.has_value())
        {
            return std::nullopt;
        }

        return RuntimeContextStore::read(*bridge_dir);
    }

    auto runtime_context_summary(
            const std::optional<RuntimeContextSnapshot>& current_snapshot,
            const std::optional<std::filesystem::path>& bridge_dir) -> std::wstring
    {
        auto snapshot = current_snapshot;
        if (!snapshot.has_value())
        {
            snapshot = read_runtime_context_snapshot(bridge_dir);
        }
        if (!snapshot.has_value())
        {
            return STR("hook=<none> | world=<none> | outer=<none>");
        }

        return RuntimeContextResolver::summarize(*snapshot);
    }

    auto runtime_context_summary(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> std::wstring
    {
        auto& context_state = runtime_state.context_subsystem_state().context_state();
        if (const auto current_summary = context_state.current_summary(); current_summary.has_value())
        {
            return *current_summary;
        }

        return runtime_context_summary(
                std::nullopt,
                bridge_dir);
    }

    auto runtime_context_object_is_not_real(std::wstring_view context_summary) -> bool
    {
        return RuntimeContextResolver::is_object_not_real(context_summary);
    }

    auto runtime_context_object_is_not_real(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> bool
    {
        return runtime_context_object_is_not_real(runtime_context_summary(runtime_state, bridge_dir));
    }

    auto is_vbonus_probe_hook_name(
            std::wstring_view hook_name,
            std::wstring_view bp_get_vbonus_name,
            std::wstring_view tres_get_vbonus_name,
            std::wstring_view wait_bp_get_vbonus_name) -> bool
    {
        return hook_name == bp_get_vbonus_name ||
               hook_name == tres_get_vbonus_name ||
               hook_name == wait_bp_get_vbonus_name;
    }

    auto should_skip_vbonus_probe_for_runtime_context(
            const std::optional<RuntimeContextSnapshot>& current_snapshot) -> bool
    {
        if (!current_snapshot.has_value())
        {
            return false;
        }

        return RuntimeContextResolver::is_cutscene_or_camera_context(*current_snapshot) ||
               is_caribbean_context(*current_snapshot) ||
               is_corona_enemy_context(*current_snapshot) ||
               is_monstropolis_lobby_unversed_context(*current_snapshot) ||
               is_keyblade_graveyard_org_fight_context(*current_snapshot) ||
               is_remind_luxord_marluxia_larxene_context(*current_snapshot) ||
               is_remind_young_xehanort_ansem_xemnas_context(*current_snapshot);
    }

    auto derive_runtime_context_from_frame_full_name(
            std::wstring_view frame_full_name,
            std::wstring_view hook_name) -> std::optional<RuntimeContextSnapshot>
    {
        return RuntimeContextResolver::derive_from_frame_full_name(frame_full_name, hook_name);
    }

    auto derive_runtime_context_from_object_full_name(
            std::wstring_view object_full_name,
            std::wstring_view hook_name) -> std::optional<RuntimeContextSnapshot>
    {
        return RuntimeContextResolver::derive_from_object_full_name(object_full_name, hook_name);
    }

    auto write_runtime_context_file(
            const std::optional<std::filesystem::path>& bridge_dir,
            const RuntimeContextSnapshot& snapshot) -> void
    {
        if (!bridge_dir.has_value())
        {
            return;
        }

        if (!RuntimeContextStore::write(*bridge_dir, snapshot))
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] Failed to write runtime context path {}\n"),
                    (*bridge_dir / channels::runtime_context_file).wstring());
        }
    }

    auto publish_synthetic_runtime_context(
            std::wstring_view hook_name,
            std::wstring_view world,
            std::wstring_view outer,
            RuntimeContextState& context_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> void
    {
        RuntimeContextSnapshot snapshot{};
        snapshot.hook = std::wstring{hook_name.empty() ? std::wstring_view{STR("<unknown>")} : hook_name};
        snapshot.world = std::wstring{world};
        snapshot.outer = std::wstring{outer};
        const auto timestamp = now_iso8601_utc();
        snapshot.opened_at.assign(timestamp.begin(), timestamp.end());

        context_state.set_current_snapshot(snapshot, outer);

        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Runtime context synthetic refresh hook={} | world={} | outer={}\n"),
                snapshot.hook,
                snapshot.world,
                snapshot.outer);
        write_runtime_context_file(bridge_dir, snapshot);
    }

    auto maybe_publish_playable_runtime_context_from_cached_pawn(
            RC::Unreal::UObject* pawn,
            RuntimeContextState& context_state,
            RuntimeObjectCache& object_cache,
            const std::optional<std::filesystem::path>& bridge_dir,
            std::chrono::steady_clock::time_point now,
            std::wstring_view pawn_source,
            bool force_publish) -> void
    {
        if (!pawn || !kh3ap::is_uobject_valid_fast(pawn) ||
            !RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn))
        {
            return;
        }

        const std::wstring pawn_name = format_object_debug_name(pawn);
        auto snapshot = derive_runtime_context_from_object_full_name(pawn_name, STR("RuntimePlayablePawnContext"));
        if (!snapshot.has_value())
        {
            return;
        }
        snapshot->pawn_source = std::wstring{pawn_source};

        if (!force_publish && context_state.should_skip_playable_publish_for_object(
                    pawn,
                    now,
                    RuntimeContextState::playable_republish_interval))
        {
            return;
        }

        const std::wstring context_key = snapshot->hook + STR("|") + snapshot->world + STR("|") + pawn_name;
        if (!force_publish && context_state.should_skip_playable_publish(
                    context_key,
                    now,
                    RuntimeContextState::playable_republish_interval))
        {
            return;
        }

        context_state.mark_playable_published(context_key, now, pawn);
        RuntimeObjectResolver::cache_world_context_object(object_cache, pawn);
        context_state.set_current_snapshot(*snapshot, snapshot->outer);
        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Runtime context playable-pawn refresh pawn={} | source={} | world={} | outer={}\n"),
                pawn_name,
                pawn_source,
                snapshot->world,
                snapshot->outer);
        write_runtime_context_file(bridge_dir, *snapshot);
    }

    auto maybe_publish_playable_runtime_context_from_cached_pawn(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir,
            std::chrono::steady_clock::time_point now,
            std::wstring_view pawn_source,
            bool force_publish) -> void
    {
        maybe_publish_playable_runtime_context_from_cached_pawn(
                runtime_state.grant_state().pawn_state().object(),
                runtime_state.context_subsystem_state().context_state(),
                runtime_state.object_cache(),
                bridge_dir,
                now,
                pawn_source,
                force_publish);
    }

    auto publish_goa_runtime_context(
            RC::Unreal::UObject* context,
            std::wstring_view hook_name,
            RuntimeContextState& context_state,
            RuntimeObjectCache& object_cache,
            RuntimeItemGrantPollState& item_grant_poll_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> void
    {
        if (context && kh3ap::is_uobject_valid_fast(context))
        {
            RuntimeObjectResolver::cache_world_context_object(object_cache, context);
        }

        publish_synthetic_runtime_context(
                hook_name,
                STR("World /Game/Maps/GardenOfAssemblage/GardenOfAssemblage.GardenOfAssemblage"),
                STR("Package /Game/Maps/GardenOfAssemblage/GardenOfAssemblage"),
                context_state,
                bridge_dir);
        item_grant_poll_state.arm();
        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] GoAReadyGrantProbe armed runtime item delivery hook={} context_object={} | context={}\n"),
                std::wstring{hook_name},
                context && kh3ap::is_uobject_valid_fast(context) ? format_object_debug_name(context) : std::wstring{STR("<null>")},
                runtime_context_summary(context_state.current_snapshot(), bridge_dir));
    }

    auto publish_goa_runtime_context(
            RC::Unreal::UObject* context,
            std::wstring_view hook_name,
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> void
    {
        publish_goa_runtime_context(
                context,
                hook_name,
                runtime_state.context_subsystem_state().context_state(),
                runtime_state.object_cache(),
                runtime_state.grant_state().item_grant_poll_state(),
                bridge_dir);
    }

    auto maybe_rearm_runtime_grant_for_ready_context(
            const RuntimeContextSnapshot& snapshot,
            const std::optional<std::filesystem::path>& request_path,
            std::chrono::steady_clock::duration runtime_item_grant_delay,
            RuntimeItemGrantPollState& item_grant_poll_state,
            const std::optional<RuntimeContextSnapshot>& current_snapshot,
            const std::optional<std::filesystem::path>& bridge_dir,
            const std::function<void()>& process_pending_runtime_item_grant) -> void
    {
        if (snapshot.world.find(STR("/Game/Maps/GardenOfAssemblage/")) == std::wstring::npos)
        {
            return;
        }
        if (!request_path.has_value() || !path_points_to_regular_file(*request_path))
        {
            return;
        }

        item_grant_poll_state.arm_ready(runtime_item_grant_delay);
        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] ReadyContextGrantProbe re-armed runtime item delivery hook={} world={} | context={}\n"),
                snapshot.hook,
                snapshot.world,
                runtime_context_summary(current_snapshot, bridge_dir));
        if (process_pending_runtime_item_grant)
        {
            process_pending_runtime_item_grant();
        }
    }

    auto maybe_rearm_runtime_grant_for_ready_context(
            const RuntimeContextSnapshot& snapshot,
            const std::optional<std::filesystem::path>& request_path,
            std::chrono::steady_clock::duration runtime_item_grant_delay,
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir,
            const std::function<void()>& process_pending_runtime_item_grant) -> void
    {
        maybe_rearm_runtime_grant_for_ready_context(
                snapshot,
                request_path,
                runtime_item_grant_delay,
                runtime_state.grant_state().item_grant_poll_state(),
                runtime_state.context_subsystem_state().context_state().current_snapshot(),
                bridge_dir,
                process_pending_runtime_item_grant);
    }

    auto update_current_frame_context(
            RC::Unreal::UFunction* frame_node,
            std::wstring_view hook_name,
            RuntimeContextState& context_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> std::optional<RuntimeContextSnapshot>
    {
        if (!frame_node)
        {
            return std::nullopt;
        }
        std::wstring frame_full_name{};
        if (!try_get_uobject_full_name_fast(frame_node, frame_full_name))
        {
            return std::nullopt;
        }
        if (is_runtime_ap_ignored_map_context(frame_full_name))
        {
            return std::nullopt;
        }
        const auto derived = derive_runtime_context_from_frame_full_name(frame_full_name, hook_name);
        if (!derived.has_value())
        {
            return std::nullopt;
        }
        const std::wstring derived_context_text = derived->world + STR(" ") + derived->outer;
        if (derived->hook == STR("BP_TresGetItem") &&
            derived_context_text.find(STR("/Game/Maps/GardenOfAssemblage/uasset/Actors/")) != std::wstring::npos)
        {
            return std::nullopt;
        }

        std::wstring current_package_path{};
        constexpr std::wstring_view package_prefix{STR("Package ")};
        if (derived->outer.starts_with(package_prefix))
        {
            current_package_path = derived->outer.substr(package_prefix.size());
        }

        const bool package_changed =
                context_state.update_current_snapshot_and_detect_package_change(
                        *derived,
                        current_package_path);

        if (!package_changed)
        {
            return std::nullopt;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Runtime context refresh hook={} | world={} | outer={}\n"),
                derived->hook,
                derived->world,
                derived->outer);
        write_runtime_context_file(bridge_dir, *derived);
        return derived;
    }

    auto update_current_frame_context(
            RC::Unreal::UFunction* frame_node,
            std::wstring_view hook_name,
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> std::optional<RuntimeContextSnapshot>
    {
        return update_current_frame_context(
                frame_node,
                hook_name,
                runtime_state.context_subsystem_state().context_state(),
                bridge_dir);
    }
}
