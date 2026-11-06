#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/bridge/catalog/bridge_lookup_store.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/hooks/kh3_runtime_object_observer.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/grants/events/event_snapshot_helpers.h"
#include "kh3ap/features/ui/map_overlay/map_overlay_service.h"
#include "kh3ap/hooks/hook_phase.h"
#include "kh3ap/bridge/rewards/loot_mapping_helpers.h"
#include "kh3ap/features/journal/ingredient_journal_service.h"
#include "kh3ap/diagnostics/retry/retry_blueprint_probe_state.h"
#include "kh3ap/diagnostics/retry/retry_point_probe_state.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/core/types.h"
#include "kh3ap/features/treasure/treasure_runtime_helpers.h"
#include "kh3ap/bridge/rewards/vbonus_reward_bundle.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kh3ap::composition_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace settings;
    struct TreasureRouteAtomicCounts
    {
        std::atomic<uint64_t> treasure_logic_actor{0};
        std::atomic<uint64_t> open_treasure_player_state{0};
        std::atomic<uint64_t> open_treasure_char_state{0};
        std::atomic<uint64_t> other{0};

        [[nodiscard]] auto snapshot() const noexcept -> kh3ap::TreasureRouteCounts
        {
            return {treasure_logic_actor.load(std::memory_order_relaxed),
                    open_treasure_player_state.load(std::memory_order_relaxed),
                    open_treasure_char_state.load(std::memory_order_relaxed),
                    other.load(std::memory_order_relaxed)};
        }

        auto record(std::wstring_view route) noexcept -> void
        {
            if (route == STR("treasure_logic_actor"))
            {
                treasure_logic_actor.fetch_add(1, std::memory_order_relaxed);
            }
            else if (route == STR("open_treasure_player_state"))
            {
                open_treasure_player_state.fetch_add(1, std::memory_order_relaxed);
            }
            else if (route == STR("open_treasure_char_state"))
            {
                open_treasure_char_state.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                other.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };
    TreasureRouteAtomicCounts s_treasure_route_observations{};
    TreasureRouteAtomicCounts s_treasure_route_wins{};

    auto record_treasure_route_observation(kh3ap::Kh3RuntimeObjectObservationKind kind) noexcept
        -> void
    {
        switch (kind)
        {
        case kh3ap::Kh3RuntimeObjectObservationKind::TreasureLogicActorConstructed:
            s_treasure_route_observations.record(STR("treasure_logic_actor"));
            break;
        case kh3ap::Kh3RuntimeObjectObservationKind::OpenTreasurePlayerState:
            s_treasure_route_observations.record(STR("open_treasure_player_state"));
            break;
        case kh3ap::Kh3RuntimeObjectObservationKind::OpenTreasureCharState:
            s_treasure_route_observations.record(STR("open_treasure_char_state"));
            break;
        case kh3ap::Kh3RuntimeObjectObservationKind::FestivalDanceStoryObjectConstructed:
        case kh3ap::Kh3RuntimeObjectObservationKind::BeatOfLeadManagerConstructed:
        case kh3ap::Kh3RuntimeObjectObservationKind::UnionCrossCommandObjectConstructed:
        case kh3ap::Kh3RuntimeObjectObservationKind::SoraPawnConstructed:
        case kh3ap::Kh3RuntimeObjectObservationKind::LuckyMarkComponentConstructed:
        case kh3ap::Kh3RuntimeObjectObservationKind::BxPortalClassConstructed:
        case kh3ap::Kh3RuntimeObjectObservationKind::DeathMirrorClassConstructed:
        case kh3ap::Kh3RuntimeObjectObservationKind::WorldBootstrapClassConstructed:
            break;
        case kh3ap::Kh3RuntimeObjectObservationKind::TreasureBoxReady:
        case kh3ap::Kh3RuntimeObjectObservationKind::NativeItemGrant:
            s_treasure_route_observations.record(STR("other"));
            break;
        }
    }

    auto increment_treasure_route_count(kh3ap::TreasureRouteCounts& counts,
                                        std::wstring_view route) noexcept -> void
    {
        if (route == STR("treasure_logic_actor"))
        {
            ++counts.treasure_logic_actor;
        }
        else if (route == STR("open_treasure_player_state"))
        {
            ++counts.open_treasure_player_state;
        }
        else if (route == STR("open_treasure_char_state"))
        {
            ++counts.open_treasure_char_state;
        }
        else
        {
            ++counts.other;
        }
    }
    auto build_unique_event_bridge_key_lookup() -> std::unordered_map<std::wstring, std::wstring>
    {
        return kh3ap::build_unique_event_bridge_key_lookup(try_get_bridge_dir(), s_debug_logging);
    }

    auto build_vbonus_bridge_key_lookup() -> std::unordered_map<std::wstring, std::wstring>
    {
        return kh3ap::build_typed_source_bridge_key_lookup(try_get_bridge_dir(), "vbonus");
    }

    auto cached_treasure_bridge_key_lookup() -> const kh3ap::TreasureBridgeKeyLookup&
    {
        // The pak source is immutable; one lookup serves the whole process.
        static const auto lookup = kh3ap::SlotDataLookupParser::typed_source_bridge_key_lookup(
                kh3ap::installed_seed_slot_data(), "treasure");
        return lookup;
    }

    auto treasure_runtime_deps() -> kh3ap::TreasureRuntimeDeps
    {
        return {s_runtime_state.treasure_state(),
                s_runtime_state.treasure_state().class_cache().actor_base_class(),
                &cached_treasure_bridge_key_lookup(),
                s_treasure_box_anim_notify_name,
                s_ap_treasure_looted_name,
                s_treasure_logic_actor_get_treasure_name_name,
                s_tres_treasure_box_open_optional_name,
                [](const RuntimeLootPayload& payload)
                {
                    return should_emit_runtime_payload(payload);
                },
                [](const RuntimeLootPayload& payload)
                {
                    if (payload.type != STR("treasure"))
                    {
                        return emit_runtime_payload_once(payload);
                    }

                    RuntimeLootPayload payload_with_telemetry = payload;
                    payload_with_telemetry.has_treasure_route_telemetry = true;
                    payload_with_telemetry.treasure_route_observations =
                        s_treasure_route_observations.snapshot();
                    payload_with_telemetry.treasure_route_wins = s_treasure_route_wins.snapshot();
                    increment_treasure_route_count(payload_with_telemetry.treasure_route_wins,
                                                   payload_with_telemetry.discovery_route);

                    const bool emitted = emit_runtime_payload_once(payload_with_telemetry);
                    if (emitted)
                    {
                        s_treasure_route_wins.record(payload_with_telemetry.discovery_route);
                    }
                    return emitted;
                }};
    }

    auto build_vbonus_reward_bundle_lookup()
        -> std::unordered_map<std::wstring, VBonusRewardBundleEntry>
    {
        return kh3ap::build_vbonus_reward_bundle_lookup(
            try_get_bridge_dir());
    }

    auto build_unique_vbonus_runtime_reward_lookup()
        -> std::unordered_map<std::wstring, std::wstring>
    {
        return kh3ap::build_unique_vbonus_runtime_reward_lookup(try_get_bridge_dir());
    }

    auto maybe_suppress_twilight_town_bistro_reward_popup(const HookTargetInfo& hook_info,
                                                          HookPhase phase,
                                                          UFunction* target_function, void* params,
                                                          std::wstring_view token_value,
                                                          std::wstring_view context_name) -> void
    {
        if (!s_enable_twilight_town_bistro_reward_popup_suppression || phase != HookPhase::Pre ||
            hook_info.function_name != s_bp_get_item_name ||
            (token_value != STR("LSIGAME01") && token_value != STR("WEP_KEYBLADE_SO_02")))
        {
            return;
        }

        kh3ap::maybe_suppress_twilight_town_bistro_reward_popup(
            s_enable_twilight_town_bistro_reward_popup_suppression, hook_info, phase,
            target_function, params, token_value, context_name, s_bp_get_item_name,
            read_runtime_context_snapshot(), composition_context_summary());
    }

    auto find_vbonus_bridge_key_for_source_id(std::wstring_view source_id)
        -> std::optional<std::wstring>
    {
        return kh3ap::find_vbonus_bridge_key_for_source_id(
            source_id, build_vbonus_reward_bundle_lookup(), build_vbonus_bridge_key_lookup());
    }

    auto find_vbonus_reward_bundle_entry(std::wstring_view source_id)
        -> std::optional<VBonusRewardBundleEntry>
    {
        return kh3ap::find_vbonus_reward_bundle_entry(source_id,
                                                       build_vbonus_reward_bundle_lookup());
    }

    auto log_vbonus_reward_bundle_summary(std::wstring_view source_id,
                                           std::wstring_view resolution_source) -> void
    {
        kh3ap::log_vbonus_reward_bundle_summary(source_id, resolution_source,
                                                 build_vbonus_reward_bundle_lookup());
    }

    auto find_vbonus_source_id_for_runtime_reward_token(std::wstring_view runtime_reward_token)
        -> std::optional<std::wstring>
    {
        return kh3ap::find_vbonus_source_id_for_runtime_reward_token(
            runtime_reward_token, build_unique_vbonus_runtime_reward_lookup());
    }

    auto emit_runtime_payload_once(const RuntimeLootPayload& payload) -> bool
    {
        return kh3ap::try_emit_runtime_payload_once(get_output_paths(), payload);
    }

    auto should_emit_runtime_payload(const RuntimeLootPayload& payload) -> bool
    {
        return kh3ap::should_emit_runtime_payload(payload, read_runtime_context_snapshot(),
                                                  s_debug_logging);
    }

    auto try_emit_vbonus_payload_from_event_array_removed(std::wstring_view source,
                                                          std::wstring_view snapshot_key,
                                                          std::wstring_view entry) -> void
    {
        kh3ap::try_emit_vbonus_payload_from_event_array_removed(
            source, snapshot_key, entry, build_vbonus_reward_bundle_lookup(),
            build_vbonus_bridge_key_lookup(),
            [](const RuntimeLootPayload& payload)
            {
                return should_emit_runtime_payload(payload);
            },
            [](const RuntimeLootPayload& payload)
            {
                return emit_runtime_payload_once(payload);
            });
    }

    auto try_build_runtime_payload(std::wstring_view hook_name, std::wstring_view token_field_name,
                                   std::wstring_view token_value, std::wstring_view actor_name)
        -> std::optional<RuntimeLootPayload>
    {
        static const auto event_lookup = build_unique_event_bridge_key_lookup();
        if (hook_name == s_bp_get_item_name)
        {
            static const std::unordered_map<std::wstring, VBonusRewardBundleEntry>
                empty_manifest_lookup{};
            static const std::unordered_map<std::wstring, std::wstring> empty_vbonus_lookup{};
            return kh3ap::try_build_runtime_payload(
                hook_name, token_field_name, token_value, actor_name, s_bp_get_item_name,
                s_bp_get_vbonus_name, s_wait_bp_get_vbonus_name, event_lookup,
                empty_manifest_lookup, empty_vbonus_lookup, s_debug_logging);
        }

        return kh3ap::try_build_runtime_payload(hook_name, token_field_name, token_value,
                                                actor_name, s_bp_get_item_name,
                                                s_bp_get_vbonus_name, s_wait_bp_get_vbonus_name,
                                                event_lookup, build_vbonus_reward_bundle_lookup(),
                                                build_vbonus_bridge_key_lookup(), s_debug_logging);
    }

    auto runtime_event_snapshot_deps() -> kh3ap::RuntimeEventSnapshotDeps
    {
        return kh3ap::make_runtime_event_snapshot_deps(
            s_runtime_state, try_emit_vbonus_payload_from_event_array_removed);
    }
} // namespace kh3ap::composition_detail
