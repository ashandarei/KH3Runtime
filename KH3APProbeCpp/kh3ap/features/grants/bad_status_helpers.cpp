#include <polyhook2/Detour/x64Detour.hpp>
#include "bad_status_helpers.h"
#include "bad_status_runtime.h"
#include "trap_catalog.h"
#include "grant_safety.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <format>
#include <utility>
#include <chrono>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace bad_status_detail;
    namespace
    {
        constexpr size_t s_apply_bad_status_virtual_slot = 0xF58 / sizeof(void*);
        BadStatusStateDiscovery discovery;
        BadStatusStateQueue state_queue;
        BadStatusParticles particles;
        PendingDirectPlayerBadStatus s_pending_direct_player_bad_status;
        uint64_t native_bad_status_original{};
        std::unique_ptr<PLH::x64Detour> native_bad_status_hook;
        FWeakObjectPtr native_burn_pawn;
        std::chrono::steady_clock::time_point native_burn_deadline;
        bool native_burn_failure_logged{};
        constexpr uint8_t burn_kind = 17;

        void apply_native_bad_status(UObject* pawn, uint8_t kind, float seconds,
                                     void* damage, void* attack)
        {
            using Apply = void (*)(UObject*, uint8_t, float, void*, void*);
            reinterpret_cast<Apply>(native_bad_status_original)(pawn, kind, seconds, damage, attack);
            if (kind != burn_kind || RuntimeGrantInternalCallDepth::is_active() ||
                !is_uobject_valid_fast(pawn) || character_swap::current_hero() != pawn ||
                !character_swap::owns_swap_pawn(pawn))
            {
                return;
            }
            auto* id = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
            if (!id || id->GetElementSize() != sizeof(uint8_t)) { return; }
            const auto player = static_cast<PlayerId>(*id->ContainerPtrToValuePtr<uint8_t>(pawn));
            if ((player != PlayerId::Kairi && player != PlayerId::Riku) ||
                *(reinterpret_cast<const uint8_t*>(pawn) + native_bad_status_kind_offset) != burn_kind)
            {
                return;
            }
            // The shipping hit accepted Burn, but the alternate can omit Sora's
            // Burn reaction. Defer queue construction until the damage stack exits.
            native_burn_pawn = make_uobject_weak(pawn);
            native_burn_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            native_burn_failure_logged = false;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Native alternate Burn accepted pawn={}\n"), pawn->GetName());
#endif
        }

        void install_native_bad_status_hook()
        {
            static bool attempted{};
            if (attempted) { return; }
            attempted = true;
            if (!character_native::verify_sites({character_native::Site::apply_bad_status})) { return; }
            const auto address = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                                 character_native::rva(character_native::Site::apply_bad_status);
            auto hook = std::make_unique<PLH::x64Detour>(address,
                reinterpret_cast<uint64_t>(&apply_native_bad_status), &native_bad_status_original);
            if (hook->hook()) { native_bad_status_hook = std::move(hook); }
        }

        auto read_bad_status_state_id(UObject* state) -> std::optional<uint8_t>
        {
            auto* field = is_uobject_valid_fast(state) ? state->GetPropertyByNameInChain(L"MyStateID") : nullptr;
            if (!field || field->GetElementSize() != sizeof(uint8_t))
            {
                return std::nullopt;
            }
            return *field->ContainerPtrToValuePtr<uint8_t>(state);
        }

        auto try_apply_sneeze_via_native_bad_status(UObject* sora_pawn,
                                                    const DirectPlayerBadStatusSpec& spec,
                                                    std::wstring* failure_details) -> bool
        {
            if (!sora_pawn)
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_sneeze_native_invalid_pawn");
                }
                return false;
            }

            void** const vtable = *reinterpret_cast<void***>(sora_pawn);
            if (!vtable)
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_sneeze_native_missing_vtable");
                }
                return false;
            }

            using ApplyBadStatusFunction =
                void (*)(UObject* pawn, uint8_t bad_status_kind, float effect_time_seconds,
                         void* damage_info, void* attack_data);
            const auto apply_bad_status =
                reinterpret_cast<ApplyBadStatusFunction>(vtable[s_apply_bad_status_virtual_slot]);
            if (!apply_bad_status)
            {
                if (failure_details)
                {
                    *failure_details = STR("direct_player_sneeze_native_function_unavailable");
                }
                return false;
            }

            // Vanilla Sneeze attack data uses a long window because Sora only
            // triggers the sneeze when a combo finisher command is reached.
            constexpr float effect_time_seconds = 15.0f;
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                apply_bad_status(sora_pawn, spec.bad_status_kind, effect_time_seconds, nullptr,
                                 nullptr);
            }
            if (failure_details)
            {
                failure_details->clear();
            }
            if constexpr (s_enable_direct_player_bad_status_diagnostics)
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Direct player Sneeze invoked through native bad-status "
                        "setter pawn={} bad_status={} effect_time={} vtable_slot=0x{:X} "
                        "function={}\n"),
                    format_object_debug_name(sora_pawn), static_cast<int>(spec.bad_status_kind),
                    effect_time_seconds, s_apply_bad_status_virtual_slot * sizeof(void*),
                    reinterpret_cast<uintptr_t>(apply_bad_status));
            }
            return true;
        }
    } // namespace

    auto PendingDirectPlayerBadStatus::arm(UObject* pawn, UObject* component, UObject* state,
                                           const DirectPlayerBadStatusSpec& effect) -> bool
    {
        sora_pawn = make_uobject_weak(pawn);
        state_component = make_uobject_weak(component);
        bad_status_state = make_uobject_weak(state);
        if (sora_pawn.Get() != pawn || state_component.Get() != component ||
            bad_status_state.Get() != state)
        {
            *this = {};
            return false;
        }
        spec = &effect;
        observed_active = false;
        return true;
    }

    auto prewarm_direct_player_bad_status_runtime_cache(UObject* pawn, UObject* component,
                                                        UObject* current) -> void
    {
        install_native_bad_status_hook();
        particles.prewarm();
        const auto context = discovery.prewarm_context(pawn, component, current);
        if (!context.state_component)
        {
            return;
        }
        state_queue.property(context.state_component);
        state_queue.prewarm_fields(context.current_state);
        if (const auto* burn_spec = find_direct_player_bad_status_spec(L"TRES_BADSTAT_BURN"))
        {
            PlayerBadStatusStateAsset asset;
            std::wstring ignored_failure;
            discovery.find_asset(context.state_component, *burn_spec, &asset, &ignored_failure);
        }
    }

    static auto queue_player_bad_status(std::wstring_view runtime_token, UObject* sora_pawn,
                                            RuntimeState& runtime_state,
                                            bool accepted_native_burn,
                                            std::wstring* failure_details) -> bool
    {
        if (failure_details)
        {
            failure_details->clear();
        }
        const auto fail = [failure_details](std::wstring details)
        {
            if (failure_details)
            {
                *failure_details = std::move(details);
            }
            return false;
        };
        const DirectPlayerBadStatusSpec* const spec =
            find_direct_player_bad_status_spec(runtime_token);
        if (!spec)
        {
            return fail(std::format(STR("unsupported_direct_player_bad_status={}"), runtime_token));
        }
        if (!sora_pawn)
        {
            return fail(STR("direct_player_bad_status_context_is_not_sora_pawn"));
        }
        auto& pawn_state = runtime_state.grant_state().pawn_state();
        // An accepted native environmental hit is already in its damage reaction,
        // where the AP trap delivery gate deliberately disallows new traps.
        const bool owned_alternate = accepted_native_burn
                ? runtime_token == L"TRES_BADSTAT_BURN" &&
                      character_swap::current_hero() == sora_pawn &&
                      character_swap::owns_swap_pawn(sora_pawn)
                : supports_alternate_bad_status(runtime_token, L"direct_player_bad_status") &&
                      runtime_alternate_bad_status_safety_status(runtime_state, sora_pawn).ready;
        if (!owned_alternate && (pawn_state.object() != sora_pawn || !pawn_state.candidate_trusted()))
        {
            return fail(STR("direct_player_bad_status_context_does_not_match_cached_sora_pawn"));
        }

        UObject* const state_component = try_read_object_pointer_in_chain(sora_pawn, L"MyStateComp");
        auto* current_state = try_read_object_pointer_in_chain(state_component, L"m_pCurrentState");
        if (!is_uobject_valid_fast(current_state) ||
            try_read_object_pointer_in_chain(current_state, L"MyCharPawn") != sora_pawn)
        {
            return fail(L"direct_player_bad_status_live_state_mismatch");
        }
        const auto sora_current_state_id = read_bad_status_state_id(current_state);
        FProperty* const state_queue_property =
            state_component ? state_queue.property(state_component) : nullptr;
        if (!state_component || !state_queue_property)
        {
            return fail(STR("direct_player_bad_status_state_component_unavailable"));
        }

        if (sora_current_state_id.has_value())
        {
            if (*sora_current_state_id == spec->state_id)
            {
                return fail(STR("direct_player_bad_status_already_active"));
            }
        }

        if (spec->bad_status_kind == sneeze_bad_status_kind)
        {
            return try_apply_sneeze_via_native_bad_status(sora_pawn, *spec, failure_details);
        }

        PlayerBadStatusStateAsset state_asset{};
        if (!discovery.find_asset(state_component, *spec, &state_asset, failure_details))
        {
            return false;
        }
        FStaticConstructObjectParameters construct_params{state_asset.state_class, state_component};
        UObject* const bad_status_state = UObjectGlobals::StaticConstructObject(construct_params);
        if (!bad_status_state)
        {
            return fail(STR("direct_player_bad_status_state_construction_failed"));
        }
        if (!state_queue.write_fields(bad_status_state, sora_pawn, *spec, state_asset,
                                      failure_details))
        {
            return false;
        }
        PendingDirectPlayerBadStatus next_pending;
        if (!next_pending.arm(sora_pawn, state_component, bad_status_state, *spec))
        {
            return fail(STR("direct_player_bad_status_weak_identity_unavailable"));
        }
        int queue_count_before = 0;
        int queue_count_after = 0;
        if (!state_queue.prepend(state_component, bad_status_state, &queue_count_before,
                                 &queue_count_after, failure_details))
        {
            return false;
        }
        s_pending_direct_player_bad_status = std::move(next_pending);

        if constexpr (s_enable_direct_player_bad_status_diagnostics)
        {
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Direct player bad status queued status={} state_id={} pawn={} "
                    "state_comp={} state={} class={} queue_before={} queue_after={}\n"),
                spec->name, static_cast<int>(spec->state_id), format_object_debug_name(sora_pawn),
                format_object_debug_name(state_component),
                format_object_debug_name(bad_status_state), state_asset.state_class->GetName(),
                queue_count_before, queue_count_after);
        }
        return true;
    }

    auto try_apply_direct_player_bad_status(std::wstring_view runtime_token, UObject* pawn,
                                            RuntimeState& runtime_state,
                                            std::wstring* failure_details) -> bool
    {
        return queue_player_bad_status(runtime_token, pawn, runtime_state, false, failure_details);
    }

    auto handle_direct_player_bad_status_state_transition(UObject* sora_pawn,
                                                          UObject* sora_state_component,
                                                          UObject* sora_current_state,
                                                          int32_t sora_current_state_id,
                                                          RuntimeState& runtime_state) -> void
    {
        auto& pending = s_pending_direct_player_bad_status;
        if (!pending.spec)
        {
            return;
        }

        const auto clear_pending = [&pending]()
        {
            pending = PendingDirectPlayerBadStatus{};
        };
        auto& pawn_state = runtime_state.grant_state().pawn_state();
        // A parked Sora event must not disarm cleanup for the possessed alternate.
        if (sora_pawn != pending.sora_pawn.Get() || sora_state_component != pending.state_component.Get())
        {
            return;
        }
        const bool owned_alternate = character_swap::current_hero() == sora_pawn &&
                character_swap::owns_swap_pawn(sora_pawn);
        if (!sora_pawn || !sora_state_component ||
            (!owned_alternate && (pawn_state.object() != sora_pawn || !pawn_state.candidate_trusted())))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Direct player bad-status cleanup disarmed because the trusted "
                    "player context changed\n"));
            clear_pending();
            return;
        }

        const bool bad_status_active =
            (pending.bad_status_state.Get() &&
             sora_current_state == pending.bad_status_state.Get()) ||
            sora_current_state_id == static_cast<int32_t>(pending.spec->state_id);
        if (bad_status_active)
        {
            if (!pending.observed_active)
            {
                pending.observed_active = true;
                if constexpr (s_enable_direct_player_bad_status_diagnostics)
                {
                    Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Direct player bad status "
                                                        "entered status={} state={}\n"),
                                                    pending.spec->name,
                                                    format_object_debug_name(sora_current_state));
                }
            }
            return;
        }

        if (!pending.observed_active)
        {
            return;
        }

        particles.clean(sora_pawn, *pending.spec, pending.bad_status_state.Get());
        clear_pending();
    }

    auto flush_native_alternate_bad_status(RuntimeState& runtime_state) -> void
    {
        auto* pawn = native_burn_pawn.Get();
        if (!pawn) { return; }
        const auto now = std::chrono::steady_clock::now();
        if (now >= native_burn_deadline ||
            character_swap::current_hero() != pawn || !character_swap::owns_swap_pawn(pawn))
        {
            native_burn_pawn = {};
            return;
        }
        // A pawn with the shared Burn state can now enter its own reaction.
        // Only queue the fallback if that transition did not occur.
        if (now < native_burn_deadline - std::chrono::milliseconds(200)) { return; }
        auto* component = try_read_object_pointer_in_chain(pawn, L"MyStateComp");
        auto* current = try_read_object_pointer_in_chain(component, L"m_pCurrentState");
        if (s_pending_direct_player_bad_status.sora_pawn.Get() == pawn ||
            read_bad_status_state_id(current) == player_state_ids::bad_stat_burn)
        {
            native_burn_pawn = {};
            return;
        }
        std::wstring failure;
        if (queue_player_bad_status(L"TRES_BADSTAT_BURN", pawn, runtime_state, true, &failure))
        {
            native_burn_pawn = {};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Native alternate Burn reaction queued pawn={}\n"), pawn->GetName());
#endif
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        else if (!native_burn_failure_logged)
        {
            native_burn_failure_logged = true;
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Native alternate Burn waiting reason={}\n"), failure);
        }
#endif
    }

    auto handle_alternate_bad_status_state_write(uintptr_t component, uintptr_t state,
                                                 RuntimeState& runtime_state) -> void
    {
        auto& pending = s_pending_direct_player_bad_status;
        if (!pending.spec)
        {
            return;
        }
        auto* pawn = pending.sora_pawn.Get();
        auto* pending_component = pending.state_component.Get();
        if (!pawn || !pending_component)
        {
            pending = {};
            return;
        }
        const auto& sora = runtime_state.grant_state().pawn_state();
        if (sora.object() == pawn && sora.candidate_trusted())
        {
            return; // The existing Sora event consumer owns this effect.
        }
        if (character_swap::current_hero() != pawn || !character_swap::owns_swap_pawn(pawn))
        {
            pending = {};
            return;
        }
        if (component != reinterpret_cast<uintptr_t>(pending_component))
        {
            return;
        }
        auto* current = reinterpret_cast<UObject*>(state);
        if (!is_uobject_valid_fast(current) || try_read_object_pointer_in_chain(current, L"MyCharPawn") != pawn)
        {
            return;
        }
        if (const auto id = read_bad_status_state_id(current))
        {
            handle_direct_player_bad_status_state_transition(pawn, pending.state_component.Get(),
                                                              current, *id, runtime_state);
        }
    }
} // namespace kh3ap
