#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/features/grants/trap_catalog.h"
#include "kh3ap/features/player/characters/character_swap.h"

#include <UnrealDef.hpp>

#include "kh3ap/hooks/blocking_ui/blocking_ui_state.h"
#include "kh3ap/features/grants/ap_ignored_map.h"
#include "kh3ap/features/player/context/context_state.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/objects/weak_object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/diagnostics/ui_diagnostic_state.h"
#include "kh3ap/bridge/text_helpers.h"
#include "kh3ap/features/grants/delivery/map_ap_grant_catalog.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <cwchar>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    using ::kh3ap::settings::s_enable_shootlock_runtime_grant_gate;

    namespace
    {
        constexpr auto max_grant_flag_index = 255;

        auto lookup_cached_runtime_sora_state_catalog_entry(
                const RuntimeSoraCurrentStateCacheView& sora_current_state_cache)
                -> const RuntimeSoraStateCatalogEntry*
        {
            const RuntimeSoraStateCatalogEntry* catalog_entry =
                    sora_current_state_cache.current_state_id.has_value()
                            ? lookup_runtime_sora_state_by_id(*sora_current_state_cache.current_state_id)
                            : nullptr;
            if (!catalog_entry && !sora_current_state_cache.state_class.empty())
            {
                catalog_entry = lookup_runtime_sora_state_by_class_name(sora_current_state_cache.state_class);
            }
            return catalog_entry;
        }

        auto format_opaque_object_address(const RC::Unreal::UObject* object) -> std::wstring
        {
            return std::format(STR("0x{:X}"), reinterpret_cast<uintptr_t>(object));
        }

        auto try_read_runtime_sora_state_object_property(
                RC::Unreal::UObject* object,
                const wchar_t* property_name) -> RC::Unreal::UObject*
        {
            if (!object || !property_name || !kh3ap::is_uobject_valid_fast(object))
            {
                return nullptr;
            }

            RC::Unreal::UClass* const object_class = object->GetClassPrivate();
            if (object_class)
            {
                for (RC::Unreal::FProperty* property : object_class->ForEachPropertyInChain())
                {
                    if (!property || property->GetName() != property_name ||
                        property->GetClass().GetName() != STR("ObjectProperty"))
                    {
                        continue;
                    }

                    if (void* const value_ptr = property->ContainerPtrToValuePtr<void>(object))
                    {
                        RC::Unreal::UObject* value = nullptr;
                        std::memcpy(&value, value_ptr, sizeof(value));
                        return value;
                    }
                }
            }

            RC::Unreal::FProperty* const property = object->GetPropertyByNameInChain(property_name);
            void* const value_ptr = object->GetValuePtrByPropertyNameInChain(property_name);
            if (!property || !value_ptr || property->GetClass().GetName() != STR("ObjectProperty"))
            {
                return nullptr;
            }

            RC::Unreal::UObject* value = nullptr;
            std::memcpy(&value, value_ptr, sizeof(value));
            return value;
        }

        auto read_u8_runtime_sora_state_property(
                RC::Unreal::UObject* object,
                const wchar_t* property_name) -> std::optional<uint8_t>
        {
            const auto value_text = try_read_object_scalar_property_as_string(object, property_name);
            if (!value_text.has_value() || value_text->empty())
            {
                return std::nullopt;
            }

            wchar_t* parse_end = nullptr;
            errno = 0;
            const unsigned long parsed = std::wcstoul(value_text->c_str(), &parse_end, 10);
            if (errno != 0 || parse_end == value_text->c_str() || parsed > max_grant_flag_index)
            {
                return std::nullopt;
            }
            return static_cast<uint8_t>(parsed);
        }
    }

    auto runtime_space_inventory_safety_status(RuntimeState& runtime_state, bool supported,
            RC::Unreal::UObject** context) -> RuntimeGrantSafetyStatus
    {
        *context=nullptr;
        if (!supported) return {false, L"space_item_requires_sora", {}};
        const auto& blocking = runtime_state.ui_diagnostic_state().blocking_ui_state();
        const auto ui = blocking.snapshot();
        if (!space::inventory_ui_allowed(ui.active, ui.reason, ui.context))
            return current_runtime_blocking_ui_status(blocking);
        *context=RuntimeSpaceService::item_delivery_context();
        return {*context!=nullptr, *context ? L"space_inventory_ready" : L"space_controller_not_ready", {}};
    }

    auto current_runtime_blocking_ui_status(const RuntimeBlockingUiState& blocking_ui_state) -> RuntimeGrantSafetyStatus
    {
        const auto snapshot = blocking_ui_state.snapshot();
        if (!snapshot.active)
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        return {
                .ready = false,
                .reason = std::wstring{STR("blocking_ui_active")},
                .details = std::format(
                        STR("reason={} context={} event={} callback_id={}"),
                        snapshot.reason.empty() ? std::wstring{STR("<unknown>")} : snapshot.reason,
                        snapshot.context.empty() ? std::wstring{STR("<unknown>")} : snapshot.context,
                        snapshot.event.empty() ? std::wstring{STR("<unknown>")} : snapshot.event,
                        snapshot.callback_id),
        };
    }

    auto current_runtime_shootlock_status(RuntimeGrantPawnState& pawn_state) -> RuntimeGrantSafetyStatus
    {
        if constexpr (!s_enable_shootlock_runtime_grant_gate)
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        RC::Unreal::UObject* const controller = pawn_state.controller_object();
        if (!controller || !kh3ap::is_uobject_valid_fast(controller))
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        RC::Unreal::UObject* const camera_shoot_flow =
                try_read_object_property_object(controller, STR("m_CameraShootFlow"));
        if (!camera_shoot_flow || !kh3ap::is_uobject_valid_fast(camera_shoot_flow))
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        return {
                .ready = false,
                .reason = std::wstring{STR("shotlock_active")},
                .details = std::format(
                        STR("controller={} m_CameraShootFlow={}"),
                        format_object_debug_name(controller),
                        format_object_debug_name(camera_shoot_flow)),
        };
    }

    auto current_runtime_bistro_return_gate_status(
            const RuntimeGrantState& grant_state,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus
    {
        if (!grant_state.bistro_return_waiting_for_map_name())
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        return {
                .ready = false,
                .reason = std::wstring{STR("bistro_return_waiting_for_map_name")},
                .details = std::format(
                        STR("source={} details={} waited_ms={}"),
                        grant_state.bistro_gate_source().empty()
                                ? std::wstring{STR("<unknown>")}
                                : grant_state.bistro_gate_source(),
                        grant_state.bistro_gate_details().empty()
                                ? std::wstring{STR("<none>")}
                                : grant_state.bistro_gate_details(),
                        grant_state.bistro_gate_wait_ms(now)),
        };
    }

    auto current_runtime_classic_kingdom_transition_gate_status(
            const RuntimeGrantState& grant_state,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus
    {
        if (!grant_state.classic_kingdom_transition_waiting())
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        return {
                .ready = false,
                .reason = std::wstring{STR("classic_kingdom_waiting_for_playable_context")},
                .details = std::format(
                        STR("source={} game_id={} waited_ms={}"),
                        grant_state.classic_kingdom_gate_source().empty()
                                ? std::wstring{STR("<unknown>")}
                                : grant_state.classic_kingdom_gate_source(),
                        grant_state.classic_kingdom_gate_game_id().empty()
                                ? std::wstring{STR("<unknown>")}
                                : grant_state.classic_kingdom_gate_game_id(),
                        grant_state.classic_kingdom_gate_wait_ms(now)),
        };
    }

    auto current_runtime_olympus_coliseum_transition_gate_status(
            const RuntimeGrantState& grant_state,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus
    {
        if (!grant_state.olympus_coliseum_transition_waiting())
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        return {
                .ready = false,
                .reason = std::wstring{
                        STR("olympus_coliseum_waiting_for_playable_context")},
                .details = std::format(
                        STR("source={} cup_id={} waited_ms={}"),
                        grant_state.olympus_coliseum_gate_source().empty()
                                ? std::wstring{STR("<unknown>")}
                                : grant_state.olympus_coliseum_gate_source(),
                        grant_state.olympus_coliseum_gate_cup_id().empty()
                                ? std::wstring{STR("<unknown>")}
                                : grant_state.olympus_coliseum_gate_cup_id(),
                        grant_state.olympus_coliseum_gate_wait_ms(now)),
        };
    }

    auto current_runtime_special_battle_mode_status(
            const RuntimeGrantState& grant_state,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus
    {
        if (!grant_state.special_battle_mode_active())
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        return {
                .ready = false,
                .reason = std::wstring{STR("special_battle_mode_active")},
                .details = std::format(
                        STR("source={} details={} waited_ms={}"),
                        grant_state.special_battle_mode_source().empty()
                                ? std::wstring{STR("<unknown>")}
                                : grant_state.special_battle_mode_source(),
                        grant_state.special_battle_mode_details().empty()
                                ? std::wstring{STR("<none>")}
                                : grant_state.special_battle_mode_details(),
                        grant_state.special_battle_mode_wait_ms(now)),
        };
    }

    auto current_runtime_unsafe_map_status(RuntimeState& runtime_state, bool allow_excluded_map_game_over) -> RuntimeGrantSafetyStatus
    {
        const auto snapshot = runtime_state.context_subsystem_state().context_state().current_snapshot();
        if (!snapshot.has_value())
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        const std::wstring context_text = snapshot->world + STR(" ") + snapshot->outer + STR(" ") + snapshot->object;
        if (is_runtime_ap_ignored_map_context(context_text))
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("ignored_runtime_map")},
                    .details = std::format(
                            STR("map=yt_50 context={}"),
                            context_text),
            };
        }
        const RuntimeMapApGrantRule* const rule = find_runtime_unsafe_ap_grant_rule(context_text);
        if (!rule || (allow_excluded_map_game_over &&
                      (runtime_state.grant_state().special_battle_mode_active() ||
                       rule->id == STR("game_levels_dw_dw_21_dw_21") ||
                       rule->id == STR("game_levels_ca_ca_04_ca_04"))))
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        return {
                .ready = false,
                .reason = std::wstring{STR("unsafe_runtime_map")},
                .details = std::format(
                        STR("rule={} match={} description={} context={}"),
                        rule->id,
                        rule->match_substring,
                        rule->description,
                        context_text),
        };
    }

    auto current_runtime_sora_state_cache_status(
            const RuntimeSoraCurrentStateCacheView& sora_current_state_cache) -> RuntimeGrantSafetyStatus
    {
        if (!sora_current_state_cache.available ||
            (!sora_current_state_cache.current_state_id.has_value() &&
             sora_current_state_cache.state_class.empty()))
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        const RuntimeSoraStateCatalogEntry* const catalog_entry =
                lookup_cached_runtime_sora_state_catalog_entry(sora_current_state_cache);

        // Match the final per-grant gate here: only an explicitly recorded-safe
        // state may enter the apply loop. Otherwise an unrecorded state can pay
        // the final reflection/diagnostic cost again on every file-poll tick.
        if (catalog_entry && catalog_entry->safety == RuntimeSoraStateSafety::Ok)
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        return {
                .ready = false,
                .reason = std::wstring{STR("runtime_sora_state_not_safe")},
                .details = std::format(
                        STR("state_class={} state_id={} safety={} catalog_match={} note={} pawn=0x{:X} state_comp_valid={} current_state_valid={}"),
                        sora_current_state_cache.state_class.empty()
                                ? std::wstring{STR("<blank>")}
                                : sora_current_state_cache.state_class,
                        sora_current_state_cache.current_state_id.has_value()
                                ? std::to_wstring(*sora_current_state_cache.current_state_id)
                                : std::wstring{STR("<none>")},
                        runtime_sora_state_safety_label(
                                catalog_entry ? catalog_entry->safety : RuntimeSoraStateSafety::Unknown),
                        catalog_entry ? std::wstring{STR("true")} : std::wstring{STR("false")},
                        catalog_entry ? catalog_entry->note : std::wstring_view{STR("unrecorded state; blocked before apply")},
                        sora_current_state_cache.pawn_address,
                        sora_current_state_cache.state_comp_valid ? std::wstring{STR("true")} : std::wstring{STR("false")},
                        sora_current_state_cache.current_state_valid ? std::wstring{STR("true")} : std::wstring{STR("false")}),
        };
    }

    auto current_runtime_bad_status_trap_sora_state_cache_status(
            const RuntimeSoraCurrentStateCacheView& sora_current_state_cache) -> RuntimeGrantSafetyStatus
    {
        const RuntimeSoraStateCatalogEntry* const catalog_entry =
                sora_current_state_cache.available
                        ? lookup_cached_runtime_sora_state_catalog_entry(sora_current_state_cache)
                        : nullptr;
        const bool allowed = catalog_entry && runtime_trap_state_allowed(catalog_entry->id);
        if (allowed)
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = std::wstring{},
            };
        }

        return {
                .ready = false,
                .reason = std::wstring{STR("bad_status_trap_sora_state_not_allowed")},
                .details = std::format(
                        STR("state_class={} state_id={} catalog_match={} pawn=0x{:X}"),
                        sora_current_state_cache.state_class.empty()
                                ? std::wstring{STR("<blank>")}
                                : sora_current_state_cache.state_class,
                        sora_current_state_cache.current_state_id.has_value()
                                ? std::to_wstring(*sora_current_state_cache.current_state_id)
                                : std::wstring{STR("<none>")},
                        catalog_entry ? std::wstring{STR("true")} : std::wstring{STR("false")},
                        sora_current_state_cache.pawn_address),
        };
    }

    static auto trap_current_state_status(RC::Unreal::UObject* pawn, RC::Unreal::UObject* component)
            -> RuntimeGrantSafetyStatus
    {
        auto* current = try_read_runtime_sora_state_object_property(component, L"m_pCurrentState");
        if (!is_uobject_valid_fast(current) ||
            try_read_runtime_sora_state_object_property(current, L"MyCharPawn") != pawn)
        {
            return {false, L"trap_live_state_unavailable", {}};
        }
        const auto state = read_u8_runtime_sora_state_property(current, L"MyStateID");
        const bool allowed = state && runtime_trap_state_allowed(*state);
        return {allowed, allowed ? L"ready" : L"trap_live_state_not_allowed", {}};
    }

    auto current_runtime_trap_live_state_status(RC::Unreal::UObject* pawn) -> RuntimeGrantSafetyStatus
    {
        return trap_current_state_status(pawn, try_read_runtime_sora_state_object_property(pawn, L"MyStateComp"));
    }

    auto runtime_trap_application_state_status(RC::Unreal::UObject* pawn) -> RuntimeGrantSafetyStatus
    {
        auto* component = try_read_runtime_sora_state_object_property(pawn, L"MyStateComp");
        auto status = trap_current_state_status(pawn, component);
        if (!status.ready)
        {
            return status;
        }

        // Airstep can be queued while the current state still reports Idle or Jump.
        // Burn prepends its own state, so check the native queue before applying a trap.
        using namespace RC::Unreal;
        using StateQueue = TArray<UObject*>;
        constexpr int max_state_queue_entries = 64;
        auto* property = component->GetPropertyByNameInChain(L"m_StateQueue");
        if (!property || property->GetClass().GetName() != L"ArrayProperty" ||
            property->GetElementSize() != sizeof(StateQueue))
        {
            return {false, L"trap_state_queue_unavailable", {}};
        }
        auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(property)->GetInner());
        if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
            inner->GetElementSize() != sizeof(UObject*))
        {
            return {false, L"trap_state_queue_unavailable", {}};
        }
        const auto* queue = property->ContainerPtrToValuePtr<StateQueue>(component);
        if (queue->Num() < 0 || queue->Num() > max_state_queue_entries ||
            queue->Max() < queue->Num() || (queue->Num() && !queue->GetData()))
        {
            return {false, L"trap_state_queue_unavailable", {}};
        }
        for (auto* queued : *queue)
        {
            if (is_uobject_valid_fast(queued) &&
                read_u8_runtime_sora_state_property(queued, L"MyStateID") == player_state_ids::athletic_flow)
            {
                return {false, L"trap_athletic_flow_queued", {}};
            }
        }
        return status;
    }

    auto runtime_alternate_bad_status_safety_status(RuntimeState& runtime_state, RC::Unreal::UObject* pawn)
            -> RuntimeGrantSafetyStatus
    {
        if (!pawn || character_swap::bad_status_context() != pawn)
        {
            return {false, L"alternate_bad_status_context_not_ready", {}};
        }
        const auto now = std::chrono::steady_clock::now();
        const std::array gates{
            current_runtime_blocking_ui_status(runtime_state.ui_diagnostic_state().blocking_ui_state()),
            current_runtime_unsafe_map_status(runtime_state),
            current_runtime_bistro_return_gate_status(runtime_state.grant_state(), now),
            current_runtime_classic_kingdom_transition_gate_status(runtime_state.grant_state(), now),
            current_runtime_olympus_coliseum_transition_gate_status(runtime_state.grant_state(), now),
            runtime_trap_application_state_status(pawn)};
        for (const auto& gate : gates)
        {
            if (!gate.ready)
            {
                return gate;
            }
        }
        return {true, L"alternate_bad_status_ready", {}};
    }

    auto runtime_persistent_reward_safety_status(
            RuntimeState& runtime_state, RC::Unreal::UObject* pawn,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus
    {
        // The active ally supplies ownership and control, never a parked Sora.
        // Shared inventory/ability storage does not exempt a grant from gameplay gates.
        if (!pawn || persistent_player_reward_context() != pawn)
        {
            return {false, L"persistent_reward_context_not_ready", {}};
        }
        const std::array gates{
            runtime_grant_context_snapshot_safety_status(
                    runtime_state, now, settings::s_runtime_item_grant_ready_context_debounce),
            current_runtime_bistro_return_gate_status(runtime_state.grant_state(), now),
            current_runtime_classic_kingdom_transition_gate_status(runtime_state.grant_state(), now),
            current_runtime_olympus_coliseum_transition_gate_status(runtime_state.grant_state(), now)};
        for (const auto& gate : gates)
        {
            if (!gate.ready)
            {
                return gate;
            }
        }
        auto* component = try_read_runtime_sora_state_object_property(pawn, L"MyStateComp");
        auto* current = try_read_runtime_sora_state_object_property(component, L"m_pCurrentState");
        if (!is_uobject_valid_fast(current) ||
            try_read_runtime_sora_state_object_property(current, L"MyCharPawn") != pawn)
        {
            return {false, L"persistent_reward_live_state_unavailable", {}};
        }
        const auto state_id = read_u8_runtime_sora_state_property(current, L"MyStateID");
        const auto* entry = state_id ? lookup_runtime_sora_state_by_id(*state_id) : nullptr;
        if (!entry && current->GetClassPrivate())
        {
            entry = lookup_runtime_sora_state_by_class_name(current->GetClassPrivate()->GetName());
        }
        if (!entry || entry->safety != RuntimeSoraStateSafety::Ok)
        {
            return {false, L"persistent_reward_live_state_not_safe", {}};
        }
        return {true, L"persistent_reward_context_ready", {}};
    }

    auto runtime_alternate_game_over_safety_status(RuntimeState& runtime_state, RC::Unreal::UObject* pawn)
            -> RuntimeGrantSafetyStatus
    {
        if (!pawn || character_swap::game_over_context() != pawn)
        {
            return {false, L"alternate_game_over_context_not_ready", {}};
        }
        const auto now = std::chrono::steady_clock::now();
        const std::array gates{
            current_runtime_blocking_ui_status(runtime_state.ui_diagnostic_state().blocking_ui_state()),
            current_runtime_unsafe_map_status(runtime_state, true),
            current_runtime_bistro_return_gate_status(runtime_state.grant_state(), now),
            current_runtime_classic_kingdom_transition_gate_status(runtime_state.grant_state(), now),
            current_runtime_olympus_coliseum_transition_gate_status(runtime_state.grant_state(), now),
            current_runtime_trap_live_state_status(pawn)};
        for (const auto& gate : gates)
        {
            if (!gate.ready) { return gate; }
        }
        return {true, L"alternate_game_over_ready", {}};
    }

    auto runtime_alternate_betrayal_safety_status(RuntimeState& runtime_state, RC::Unreal::UObject* pawn)
            -> RuntimeGrantSafetyStatus
    {
        const auto context = runtime_alternate_game_over_safety_status(runtime_state, pawn);
        if (!context.ready) { return context; }
        const auto world = current_runtime_unsafe_map_status(runtime_state);
        if (!world.ready) { return world; }
        return runtime_trap_application_state_status(pawn);
    }

    auto update_runtime_grant_pawn_presence_status(
            RuntimeGrantPawnState& pawn_state,
            const std::vector<RC::Unreal::UObject*>& candidates,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus
    {
        if (candidates.empty())
        {
            pawn_state.reset();
            return {
                    .ready = false,
                    .reason = std::wstring{STR("no_player_pawn")},
                    .details = std::wstring{STR("no Sora/player pawn candidate found")},
            };
        }

        std::wstring first_candidate{};
        for (RC::Unreal::UObject* const candidate : candidates)
        {
            if (!candidate)
            {
                continue;
            }

            const bool trusted_cached_candidate =
                    candidate == pawn_state.object() &&
                    pawn_state.candidate_trusted();
            if (!trusted_cached_candidate && !kh3ap::is_uobject_valid_fast(candidate))
            {
                if (candidate == pawn_state.object())
                {
                    pawn_state.reset();
                }
                continue;
            }

            const std::wstring candidate_name = trusted_cached_candidate
                    ? pawn_state.name()
                    : format_object_debug_name(candidate);
            if (first_candidate.empty())
            {
                first_candidate = candidate_name;
            }
            if (candidate_name.find(STR("Default__")) != std::wstring::npos)
            {
                continue;
            }

            const std::wstring class_name = trusted_cached_candidate
                    ? std::wstring{}
                    : (candidate->GetClassPrivate() ? candidate->GetClassPrivate()->GetName() : std::wstring{});
            if (RuntimeTextHelpers::contains_death_or_game_over_marker(candidate_name) ||
                RuntimeTextHelpers::contains_death_or_game_over_marker(class_name))
            {
                return {
                        .ready = false,
                        .reason = std::wstring{STR("player_not_live")},
                        .details = std::format(STR("dead_state_candidate object={} class={}"), candidate_name, class_name),
                };
            }

            pawn_state.update_candidate(candidate, candidate_name, now, weak_uobject_resolver(candidate));
            const auto stable_for = pawn_state.stable_for(now);
            if (stable_for < RuntimeGrantPawnState::stability_delay)
            {
                return {
                        .ready = false,
                        .reason = std::wstring{STR("player_pawn_not_stable")},
                        .details = std::format(
                                STR("pawn={} stable_ms={} required_ms={}"),
                                candidate_name,
                                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(stable_for).count()),
                                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                              RuntimeGrantPawnState::stability_delay)
                                                              .count())),
                };
            }

            return {
                    .ready = true,
                    .reason = std::wstring{STR("ready")},
                    .details = candidate_name,
            };
        }

        pawn_state.reset();
        return {
                .ready = false,
                .reason = std::wstring{STR("no_live_player_pawn")},
                .details = first_candidate.empty() ? std::wstring{STR("<none>")} : first_candidate,
        };
    }

    auto runtime_grant_playable_safety_status(
            const RuntimeBlockingUiState& blocking_ui_state,
            RuntimeGrantPawnState& pawn_state,
            const std::vector<RC::Unreal::UObject*>& candidates,
            std::chrono::steady_clock::time_point now) -> RuntimeGrantSafetyStatus
    {
        const auto blocking_ui_status = current_runtime_blocking_ui_status(blocking_ui_state);
        if (!blocking_ui_status.ready)
        {
            return blocking_ui_status;
        }
        const auto shootlock_status = current_runtime_shootlock_status(pawn_state);
        if (!shootlock_status.ready)
        {
            return shootlock_status;
        }
        return update_runtime_grant_pawn_presence_status(pawn_state, candidates, now);
    }

    auto runtime_cached_sora_pawn_is_available(RuntimeState& runtime_state) -> bool
    {
        const auto& pawn_state = runtime_state.grant_state().pawn_state();
        return pawn_state.object() &&
               pawn_state.candidate_trusted();
    }

    auto runtime_cached_sora_state_matches(
            RuntimeState& runtime_state,
            uint8_t state_id,
            std::wstring_view state_class_name) -> bool
    {
        if (!runtime_cached_sora_pawn_is_available(runtime_state))
        {
            return false;
        }

        RC::Unreal::UObject* const cached_pawn = runtime_state.grant_state().pawn_state().object();
        RC::Unreal::UObject* const state_comp =
                try_read_runtime_sora_state_object_property(cached_pawn, STR("MyStateComp"));
        RC::Unreal::UObject* const current_state =
                try_read_runtime_sora_state_object_property(state_comp, STR("m_pCurrentState"));
        if (!current_state || !kh3ap::is_uobject_valid_fast(current_state))
        {
            return false;
        }

        const std::optional<uint8_t> current_state_id =
                read_u8_runtime_sora_state_property(current_state, STR("MyStateID"));
        if (current_state_id.has_value() && *current_state_id == state_id)
        {
            return true;
        }

        const std::wstring current_state_class =
                current_state->GetClassPrivate() ? current_state->GetClassPrivate()->GetName() : std::wstring{};
        return !state_class_name.empty() && current_state_class == state_class_name;
    }

    auto runtime_cached_sora_state_matches_kind(
            RuntimeState& runtime_state,
            RuntimeSoraStateKind kind) -> bool
    {
        const RuntimeSoraStateCatalogEntry* const entry = lookup_runtime_sora_state_by_kind(kind);
        if (!entry)
        {
            return false;
        }
        return runtime_cached_sora_state_matches(runtime_state, entry->id, entry->class_name);
    }

    auto runtime_grant_final_sora_state_safety_status(
            RuntimeState& runtime_state,
            RC::Unreal::UObject* world_context_object,
            const RuntimeSoraCurrentStateCacheView* sora_current_state_cache) -> RuntimeGrantSafetyStatus
    {
        if (!world_context_object)
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("final_world_context_object_unavailable")},
                    .details = std::wstring{STR("<null>")},
            };
        }
        RC::Unreal::UObject* const cached_pawn = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:cached_pawn")};
            return runtime_state.grant_state().pawn_state().object();
        }();
        const bool world_is_cached_pawn = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:world_cached_match")};
            return world_context_object == cached_pawn;
        }();
        if (!world_is_cached_pawn)
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("final_world_context_not_cached_sora_pawn")},
                    .details = [&]() {
                        const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:context_mismatch_details")};
                        return std::format(
                                STR("world={} cached={}"),
                                format_opaque_object_address(world_context_object),
                                format_opaque_object_address(cached_pawn));
                    }(),
            };
        }

        const bool cached_pawn_trusted = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:cached_pawn_trusted")};
            const auto& pawn_state = runtime_state.grant_state().pawn_state();
            return cached_pawn && pawn_state.candidate_trusted();
        }();
        if (!cached_pawn_trusted)
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("final_cached_sora_pawn_untrusted")},
                    .details = [&]() {
                        const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:cached_pawn_fail_details")};
                        return runtime_state.grant_state().pawn_state().name().empty()
                                ? std::wstring{STR("<uncached>")}
                                : runtime_state.grant_state().pawn_state().name();
                    }(),
            };
        }

        RC::Unreal::UObject* state_comp = nullptr;
        RC::Unreal::UObject* current_state = nullptr;
        bool current_state_real = false;
        std::optional<uint8_t> current_state_id{};
        std::wstring state_class{};
        bool current_state_matches_cached_pawn = false;
        const bool cached_current_state_ready = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:cached_current_state")};
            return sora_current_state_cache &&
                   sora_current_state_cache->available &&
                   sora_current_state_cache->state_comp_valid &&
                   sora_current_state_cache->current_state_valid &&
                   sora_current_state_cache->pawn_address == reinterpret_cast<uintptr_t>(cached_pawn) &&
                   sora_current_state_cache->state_comp &&
                   sora_current_state_cache->current_state &&
                   (sora_current_state_cache->current_state_id.has_value() ||
                    !sora_current_state_cache->state_class.empty());
        }();
        if (cached_current_state_ready)
        {
            state_comp = sora_current_state_cache->state_comp;
            current_state = sora_current_state_cache->current_state;
            current_state_real = true;
            current_state_id = sora_current_state_cache->current_state_id;
            state_class = sora_current_state_cache->state_class;
            current_state_matches_cached_pawn = true;
        }
        else if (sora_current_state_cache)
        {
            // The runtime item path supplies this cache. Fail closed instead of
            // reflecting through the cached pawn or its state objects here.
            return {
                    .ready = false,
                    .reason = std::wstring{STR("final_sora_state_cache_unavailable")},
                    .details = std::format(
                            STR("pawn={} cache_available={} state_comp_valid={} current_state_valid={} cache_pawn=0x{:X}"),
                            format_opaque_object_address(cached_pawn),
                            sora_current_state_cache->available ? STR("true") : STR("false"),
                            sora_current_state_cache->state_comp_valid ? STR("true") : STR("false"),
                            sora_current_state_cache->current_state_valid ? STR("true") : STR("false"),
                            sora_current_state_cache->pawn_address),
            };
        }
        else
        {
            state_comp = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:read_MyStateComp")};
                return try_read_runtime_sora_state_object_property(cached_pawn, STR("MyStateComp"));
            }();
            current_state = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:read_m_pCurrentState")};
                return try_read_runtime_sora_state_object_property(state_comp, STR("m_pCurrentState"));
            }();
            current_state_real = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:current_state_real")};
                return current_state && kh3ap::is_uobject_valid_fast(current_state);
            }();
        }
        if (!current_state_real)
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("final_sora_state_unavailable")},
                    .details = [&]() {
                        const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:state_unavailable_details")};
                        return std::format(
                                STR("pawn={} state_comp={} current_state={}"),
                                format_opaque_object_address(cached_pawn),
                                format_object_debug_name(state_comp),
                                format_object_debug_name(current_state));
                    }(),
            };
        }

        if (!cached_current_state_ready)
        {
            current_state_id = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:read_MyStateID")};
                return read_u8_runtime_sora_state_property(current_state, STR("MyStateID"));
            }();
            state_class = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:state_class")};
                return current_state->GetClassPrivate() ? current_state->GetClassPrivate()->GetName() : std::wstring{};
            }();
        }
        const RuntimeSoraStateCatalogEntry* state_catalog_entry = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:catalog_lookup")};
            const RuntimeSoraStateCatalogEntry* entry =
                    current_state_id.has_value() ? lookup_runtime_sora_state_by_id(*current_state_id) : nullptr;
            if (!entry && !state_class.empty())
            {
                entry = lookup_runtime_sora_state_by_class_name(state_class);
            }
            return entry;
        }();
        const RuntimeSoraStateSafety state_safety = [&]() {
            const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:safety_result")};
            return state_catalog_entry ? state_catalog_entry->safety : RuntimeSoraStateSafety::NotOk;
        }();
        if (state_safety != RuntimeSoraStateSafety::Ok)
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("final_sora_state_not_safe")},
                    .details = [&]() {
                        const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:not_safe_details")};
                        return std::format(
                                STR("state_class={} state_id={} safety={} catalog_match={} state={}"),
                                state_class.empty() ? std::wstring{STR("<blank>")} : state_class,
                                current_state_id.has_value() ? std::to_wstring(*current_state_id) : std::wstring{STR("<none>")},
                                runtime_sora_state_safety_label(state_safety),
                                state_catalog_entry ? std::wstring{STR("true")} : std::wstring{STR("false")},
                                format_object_debug_name(current_state));
                    }(),
            };
        }

        RC::Unreal::UObject* current_state_pawn = nullptr;
        bool current_state_pawn_mismatch = false;
        if (!current_state_matches_cached_pawn)
        {
            current_state_pawn = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:read_MyCharPawn")};
                return try_read_runtime_sora_state_object_property(current_state, STR("MyCharPawn"));
            }();
            current_state_pawn_mismatch = [&]() {
                const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:state_pawn_match")};
                return current_state_pawn && kh3ap::is_uobject_valid_fast(current_state_pawn) &&
                       current_state_pawn != cached_pawn &&
                       !RuntimeObjectResolver::is_sora_ability_pawn_candidate(current_state_pawn);
            }();
        }
        if (current_state_pawn_mismatch)
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("final_sora_state_pawn_mismatch")},
                    .details = [&]() {
                        const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:pawn_mismatch_details")};
                        return std::format(
                                STR("state_class={} state_id={} current_state_pawn={} cached={}"),
                                state_class,
                                current_state_id.has_value() ? std::to_wstring(*current_state_id) : std::wstring{STR("<none>")},
                                format_object_debug_name(current_state_pawn),
                                format_opaque_object_address(cached_pawn));
                    }(),
            };
        }

        return {
                .ready = true,
                .reason = std::wstring{STR("ready")},
                .details = [&]() {
                    const RuntimeHotPathProfileScope profile{STR("runtime_item_grant:final_safety:ready_details")};
                    return std::format(
                            STR("state_class={} state_id={} pawn={}"),
                            state_class,
                            current_state_id.has_value() ? std::to_wstring(*current_state_id) : std::wstring{STR("<none>")},
                            runtime_state.grant_state().pawn_state().name().empty()
                                    ? format_opaque_object_address(cached_pawn)
                                    : runtime_state.grant_state().pawn_state().name());
                }(),
        };
    }

    auto collect_world_context_candidates(RuntimeState& runtime_state, RC::Unreal::UObject* preferred_object)
            -> std::vector<RC::Unreal::UObject*>
    {
        return RuntimeObjectResolver::collect_world_context_candidates(
                runtime_state.object_cache(),
                runtime_state.treasure_state().class_cache().actor_base_class(),
                preferred_object,
                collect_sora_ability_pawn_candidates(runtime_state, preferred_object));
    }

    auto collect_sora_ability_pawn_candidates(RuntimeState& runtime_state, RC::Unreal::UObject* preferred_object)
            -> std::vector<RC::Unreal::UObject*>
    {
        return RuntimeObjectResolver::collect_sora_ability_pawn_candidates(
                preferred_object,
                runtime_state.object_cache().last_world_context_object());
    }

    auto collect_cached_runtime_grant_pawn_candidates(RuntimeState& runtime_state, RC::Unreal::UObject* preferred_object)
            -> std::vector<RC::Unreal::UObject*>
    {
        std::vector<RC::Unreal::UObject*> candidates{};
        const auto append_if_missing = [&candidates](RC::Unreal::UObject* object) {
            if (object && std::find(candidates.begin(), candidates.end(), object) == candidates.end())
            {
                candidates.push_back(object);
            }
        };
        const auto append_if_sora = [&append_if_missing](RC::Unreal::UObject* object) {
            if (RuntimeObjectResolver::is_sora_ability_pawn_candidate(object))
            {
                append_if_missing(object);
            }
        };

        const auto& pawn_state = runtime_state.grant_state().pawn_state();
        const bool cached_pawn_trusted =
                pawn_state.object() && pawn_state.candidate_trusted();
        const auto append_candidate = [&](RC::Unreal::UObject* object) {
            if (cached_pawn_trusted && object == pawn_state.object())
            {
                append_if_missing(object);
                return;
            }
            append_if_sora(object);
        };
        append_candidate(preferred_object);
        append_candidate(pawn_state.object());
        append_candidate(runtime_state.object_cache().last_world_context_object());
        return candidates;
    }

    auto collect_fast_runtime_item_grant_world_context_candidates(RuntimeState& runtime_state, RC::Unreal::UObject* preferred_object)
            -> std::vector<RC::Unreal::UObject*>
    {
        (void)preferred_object;
        std::vector<RC::Unreal::UObject*> candidates{};
        const auto& pawn_state = runtime_state.grant_state().pawn_state();
        RC::Unreal::UObject* const cached_pawn = pawn_state.object();
        if (cached_pawn && pawn_state.candidate_trusted())
        {
            candidates.push_back(cached_pawn);
        }
        return candidates;
    }

    auto runtime_grant_context_snapshot_safety_status(
            RuntimeState& runtime_state,
            std::chrono::steady_clock::time_point now,
            std::chrono::steady_clock::duration ready_context_debounce,
            bool allow_special_battle_mode,
            bool allow_excluded_map_game_over) -> RuntimeGrantSafetyStatus
    {
        const auto blocking_ui_status = current_runtime_blocking_ui_status(
                runtime_state.ui_diagnostic_state().blocking_ui_state());
        if (!blocking_ui_status.ready)
        {
            return blocking_ui_status;
        }

        if (!allow_special_battle_mode)
        {
            const auto special_battle_mode_status =
                    current_runtime_special_battle_mode_status(runtime_state.grant_state(), now);
            if (!special_battle_mode_status.ready)
            {
                return special_battle_mode_status;
            }
        }

        const auto snapshot = runtime_state.context_subsystem_state().context_state().current_snapshot();
        if (!snapshot.has_value())
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("missing_runtime_context_snapshot")},
                    .details = std::wstring{},
            };
        }
        const auto snapshot_age = runtime_state.context_subsystem_state().context_state().current_snapshot_age(now);
        if (!snapshot_age.has_value())
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("runtime_context_age_unknown")},
                    .details = snapshot->hook,
            };
        }
        if (*snapshot_age < ready_context_debounce)
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("runtime_context_ready_debounce")},
                    .details = std::format(
                            STR("hook={} age_ms={} debounce_ms={}"),
                            snapshot->hook,
                            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(*snapshot_age).count()),
                            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(ready_context_debounce).count())),
            };
        }

        const std::wstring context_text = snapshot->world + STR(" ") + snapshot->outer + STR(" ") + snapshot->object;
        if (is_runtime_ap_ignored_map_context(context_text))
        {
            return {
                    .ready = false,
                    .reason = std::wstring{STR("ignored_runtime_map")},
                    .details = std::format(
                            STR("map=yt_50 context={}"),
                            context_text),
            };
        }
        const auto map_status = current_runtime_unsafe_map_status(runtime_state, allow_excluded_map_game_over);
        if (!map_status.ready)
        {
            return map_status;
        }
        if (snapshot->hook == STR("RuntimePlayablePawnContext") &&
            !snapshot->object.empty() &&
            snapshot->object.find(STR("real=false")) == std::wstring::npos)
        {
            return {
                    .ready = true,
                    .reason = std::wstring{STR("safe_runtime_context_snapshot")},
                    .details = snapshot->object,
            };
        }
        return {
                .ready = false,
                .reason = std::wstring{STR("unsafe_runtime_context_snapshot")},
                .details = context_text,
        };
    }

}
