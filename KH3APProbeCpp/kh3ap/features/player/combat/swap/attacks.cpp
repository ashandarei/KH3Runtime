#include "swap_impl.h"
#include "swap_state.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/grants/bad_status_runtime.h"
#include "kh3ap/features/grants/trap_catalog.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/characters/character_idle.h"
#include "kh3ap/features/kh02/challenges/kh02_damage_event_decoder.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include <Unreal/Property/FStructProperty.hpp>
#include <algorithm>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace combat_swap_detail;

    constexpr size_t hud_selected_main_command_offset = 0x6f0;
    constexpr size_t hud_command_state_offset = 0xac8;
    constexpr int hud_main_command_state = 1;

    int CharacterCombatSwap::Impl::select_riku(UObject* current)
    {
        return active->select(current, active->original_alternates[0]);
    }

    int CharacterCombatSwap::Impl::select_kairi(UObject* current)
    {
        return active->select(current, active->original_alternates[1]);
    }

    int CharacterCombatSwap::Impl::select_roxas(UObject* current)
    {
        return active->select(current, active->original_alternates[2]);
    }

    int CharacterCombatSwap::Impl::select_sora(UObject* current)
    {
        return active->select(current, active->original_sora);
    }

    int CharacterCombatSwap::Impl::select_aqua(UObject* current)
    {
        return active->select(current, active->original_aqua);
    }

    void CharacterCombatSwap::Impl::prepare_attack(UObject* current)
    {
        auto* self = active;
        reinterpret_cast<void (*)(UObject*)>(self->original_prepare)(current);
        if (!self->resuming_swing)
        {
            return;
        }
        try
        {
            auto* pawn = object(current, L"MyCharPawn");
            if (player_id(pawn) == self->target &&
                actor_owning_world(pawn) == self->request_world.Get() &&
                attack_state(current, pawn))
            {
                // Keep an ongoing air swing even if this selector's ground
                // proximity test sees a surface during the handoff.
                *reinterpret_cast<uint8_t*>(reinterpret_cast<std::byte*>(current) +
                                            combo_air_offset) = self->swing_air;
            }
        }
        catch (const std::exception& failure)
        {
            self->error = failure.what();
        }
    }

    void CharacterCombatSwap::Impl::enter_attack(UObject* current)
    {
        auto* self = active;
        auto* pawn = object(current, L"MyCharPawn");
        const bool resume = self->resuming_swing && player_id(pawn) == self->target &&
                            actor_owning_world(pawn) == self->request_world.Get() &&
                            attack_state(current, pawn);
        reinterpret_cast<void (*)(UObject*)>(self->original_attack)(current);
        if (!resume)
        {
            return;
        }
        try
        {
            auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
            const float length = animation_value(anim, L"SQEX_AnimGetCurrentLength");
            if (length <= 0.0f)
            {
                throw std::runtime_error("kh02_swing_resume_animation_unavailable");
            }
            Call rate(anim, L"SQEX_AnimSetCurrentPlayRate");
            rate.name(L"InSlotName", L"FullBody");
            rate.number(L"InPlayRate", self->swing_progress_rate * length);
            rate.run();
            Call seek(anim, L"SQEX_AnimSetCurrentTime");
            seek.name(L"InSlotName", L"FullBody");
            seek.number(L"InTime", self->swing_phase * length);
            seek.run();
            // Seeking skips earlier animation notifications. Carry the
            // outgoing swing's already-open input/cancel gates through the
            // incoming animation's normal notify handlers.
            if (self->swing_input_ready)
            {
                Call input(anim, L"NotifyEnableInput");
                input.run();
            }
            if (self->swing_change_ready)
            {
                Call cancel(anim, L"NotifyEnableChange");
                cancel.run();
            }
            self->swing_pending = false;
            self->next_index = field<uint8_t>(current, combo_finisher_offset)
                                   ? -1
                                   : field<int>(current, combo_index_offset) + 1;
            self->air = field<uint8_t>(current, combo_air_offset) != 0;
            self->incoming = make_uobject_weak(pawn);
            const float play_rate = animation_value(anim, L"SQEX_AnimGetCurrentPlayRate");
            const float remaining =
                (1.0f - self->swing_phase) * length / std::max(play_rate, 0.01f);
            self->combo_until = Clock::now() + combo_grace +
                                std::chrono::duration_cast<Clock::duration>(
                                    std::chrono::duration<float>(remaining));
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            self->swing_resume = {{"player", player_id(pawn)},
                                  {"index", field<int>(current, combo_index_offset)},
                                  {"source_phase", self->swing_phase},
                                  {"time", animation_value(anim, L"SQEX_AnimGetCurrentTime")},
                                  {"length", length},
                                  {"source_progress_rate", self->swing_progress_rate},
                                  {"play_rate", play_rate},
                                  {"air", field<uint8_t>(current, combo_air_offset) != 0}};
#endif
        }
        catch (const std::exception& failure)
        {
            self->error = failure.what();
        }
    }

    void CharacterCombatSwap::Impl::resume_swing(UObject* pawn)
    {
        if (!swing_pending)
        {
            return;
        }
        auto* component = object(pawn, L"MyStateComp");
        if (!is_uobject_valid_fast(component) ||
            component->GetClassPrivate()->GetName() != L"TresStateQueueComponent")
        {
            throw std::runtime_error("kh02_swing_state_queue");
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        resuming_swing = true;
        try
        {
            // Use the owning native queue's clear/push/update lifecycle.
            // The new hero constructs its own attack, targets and animation.
            reinterpret_cast<void (*)(UObject*)>(base + queue_clear_rva)(component);
            auto* attack = reinterpret_cast<UObject* (*)(UObject*, int)>(base + queue_push_rva)(
                component, attack_kind(target));
            if (!attack_state(attack, pawn))
            {
                throw std::runtime_error("kh02_swing_attack_creation");
            }
            set_property(attack, L"m_pTargetPawn", swing_target.Get());
            set_property(attack, L"m_HomingTargetComponent", swing_homing.Get());
            reinterpret_cast<void (*)(UObject*, float)>(base + queue_update_rva)(component, 0.0f);
        }
        catch (...)
        {
            resuming_swing = false;
            throw;
        }
        resuming_swing = false;
    }

    int CharacterCombatSwap::Impl::select(UObject* current, uint64_t original)
    {
        auto* pawn = object(current, L"MyCharPawn");
        bool applied = false;
        try
        {
            if (resuming_swing && player_id(pawn) == target &&
                actor_owning_world(pawn) == request_world.Get())
            {
                *reinterpret_cast<int*>(reinterpret_cast<std::byte*>(current) +
                                        combo_index_offset) = swing_index;
                *reinterpret_cast<uint8_t*>(reinterpret_cast<std::byte*>(current) +
                                            combo_air_offset) = swing_air;
                swing_hits.resume();
                applied = true;
            }
            else if (const auto* intent = attack_buffer.front();
                     intent && intent->attempted &&
                     pawn == local_player_pawn(request_world.Get()) &&
                     attack_state(current, pawn) && actor_owning_world(pawn) == request_world.Get())
            {
                if (field<int>(current, combo_index_offset) == 0 &&
                    (field<uint8_t>(current, combo_air_offset) != 0) == intent->air)
                {
                    *reinterpret_cast<int*>(reinterpret_cast<std::byte*>(current) +
                                            combo_index_offset) = intent->index;
                    applied = true;
                }
                next_index = -1;
                incoming = {};
            }
            else if (next_index >= 0 && incoming.Get() == pawn && Clock::now() <= combo_until &&
                     pawn == local_player_pawn(request_world.Get()) &&
                     actor_owning_world(pawn) == request_world.Get() && attack_state(current, pawn))
            {
                if (field<int>(current, combo_index_offset) == 0 &&
                    (field<uint8_t>(current, combo_air_offset) != 0) == air)
                {
                    *reinterpret_cast<int*>(reinterpret_cast<std::byte*>(current) +
                                            combo_index_offset) = next_index;
                    applied = true;
                }
                // Consume only this first native attack selection. A later
                // attack/finisher must advance under its own native rules.
                next_index = -1;
                incoming = {};
            }
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            next_index = -1;
            incoming = {};
        }
        const int selected = reinterpret_cast<Select>(original)(current);
        [[maybe_unused]] uint64_t input_serial{};
        if (actor_owning_world(pawn) == swap_context_world() && attack_state(current, pawn) &&
            local_player_pawn(swap_context_world()) == pawn)
        {
            character_idle::clear_sneeze_finisher();
            if (field<uint8_t>(pawn, bad_status_detail::native_bad_status_kind_offset) ==
                bad_status_detail::sneeze_bad_status_kind)
            {
                const auto hero = player_id(pawn);
                // Roxas's native finisher selector returns 230 without setting the shared flag.
                constexpr int roxas_finisher = 230;
                const bool finisher =
                    field<uint8_t>(current, combo_finisher_offset) != 0 ||
                    (hero == player_id_value(PlayerId::Roxas) && selected == roxas_finisher);
                if (finisher && alternate_bad_status_player_allowed(hero) &&
                    character_swap::owns_swap_pawn(pawn))
                {
                    character_idle::prepare_sneeze_finisher(
                        pawn, current, field<uint8_t>(current, combo_air_offset) != 0);
                }
            }
            if (!resuming_swing)
            {
                swing_hits.begin();
                if (const auto* intent = attack_buffer.front();
                    intent &&
                    (intent->attempted || intent->index == field<int>(current, combo_index_offset)))
                {
                    input_serial = intent->serial;
                    attack_buffer.accepted();
                    attack_input_until = {};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    ++accepted_presses;
#endif
                }
            }
            swing_owner = make_uobject_weak(pawn);
            swing_state = make_uobject_weak(current);
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (actor_owning_world(pawn) == swap_context_world() && attack_state(current, pawn) &&
            local_player_pawn(swap_context_world()) == pawn)
        {
            selections.push_back({{"player", player_id(pawn)},
                                  {"index", field<int>(current, combo_index_offset)},
                                  {"attack", selected},
                                  {"air", field<uint8_t>(current, combo_air_offset) != 0},
                                  {"finisher", field<uint8_t>(current, combo_finisher_offset) != 0},
                                  {"resumed_swing", resuming_swing},
                                  {"handoff", applied}});
            selections.back()["input_serial"] = input_serial;
            if (selections.size() > 32)
            {
                selections.pop_front();
            }
        }
#endif
        return selected;
    }

    std::optional<bool> CharacterCombatSwap::Impl::attack_command_selected(UObject* pawn)
    {
        auto* manager = object(object(kh02_current_viewport(), L"GameInstance"), L"m_UIManager");
        auto* controller = object(manager, L"m_pPlayerController");
        if (!is_uobject_valid_fast(controller) ||
            (controller != object(pawn, L"Controller") &&
             (!queued || controller != object(outgoing.Get(), L"Controller"))))
        {
            return std::nullopt;
        }
        auto* active_hud = object(manager, L"m_ActiveHUD");
        auto* hud = object(object(active_hud, L"TresCockpit"), L"m_pHudCommand");
        if (!is_uobject_valid_fast(hud) ||
            hud->GetClassPrivate()->GetName() != L"TresUIP_HudCommand" ||
            actor_owning_world(active_hud) != actor_owning_world(pawn) ||
            object(hud, L"m_pUIManager") != manager)
        {
            return std::nullopt;
        }
        // The retail HUD owns the selection, even while the pawn is Idle.
        // Validate its reflected command-array layout before reading the
        // adjacent native main-selection/state fields used by its reset.
        auto* commands = movement_properties.resolve(hud, L"m_mainCmdInfoAry");
        if (!commands || !commands->IsA<FStructProperty>() ||
            commands->GetOffset_Internal() != 0x1d0 || commands->GetElementSize() != 32 ||
            commands->GetArrayDim() != 4)
        {
            return std::nullopt;
        }
        const int command_state = field<int>(hud, hud_command_state_offset);
        if (command_state == 0)
        {
            if (field<int>(hud, hud_selected_main_command_offset) != 0)
            {
                return false;
            }
            // A resumed native swing already establishes Attack ownership
            // while its new HUD initializes. Preserve that combo's input.
            if (attack_state(state(pawn), pawn))
            {
                return true;
            }
            return std::nullopt;
        }
        return command_state == hud_main_command_state &&
               field<int>(hud, hud_selected_main_command_offset) == 0;
    }

    bool CharacterCombatSwap::Impl::attack_key(int controller, std::wstring_view key, int event)
    {
        if (replaying_attack || (key != L"Gamepad_FaceButton_Bottom" && key != L"LeftMouseButton"))
        {
            return false;
        }
        return attack_input.route(
            controller, std::wstring(key), event,
            [&]
            {
                if (event != 0)
                {
                    return true;
                }
                const auto now = Clock::now();
                if ((!queued && now > attack_input_until && !attack_buffer.front()) ||
                    request_world.Get() != swap_context_world() || Kh02Story::challenges_open())
                {
                    return false;
                }
                auto* pawn = local_player_pawn(request_world.Get());
                if (!is_uobject_valid_fast(pawn) || attack_kind(player_id(pawn)) < 0)
                {
                    return false;
                }
                if (!attack_command_selected(pawn).value_or(queued))
                {
                    attack_buffer.clear();
                    return false;
                }
                Call paused(L"/Script/Engine.Default__GameplayStatics", L"IsGamePaused");
                paused.object(L"WorldContextObject", pawn);
                paused.run();
                if (paused.boolean_result())
                {
                    return false;
                }
                auto* current = state(pawn);
                const int state_kind = state_id(current);
                if (state_kind != player_state_ids::idle && state_kind != player_state_ids::run &&
                    state_kind != player_state_ids::jump && !attack_state(current, pawn))
                {
                    return false;
                }
                int wanted = std::max(next_index, 0);
                bool wanted_air = air;
                if (attack_state(current, pawn))
                {
                    wanted = field<uint8_t>(current, combo_finisher_offset)
                                 ? 0
                                 : field<int>(current, combo_index_offset) + 1;
                    wanted_air = field<uint8_t>(current, combo_air_offset) != 0;
                }
                const auto deadline = queued ? now + switch_timeout + combo_grace
                                             : std::max(combo_until, now + combo_grace);
                if (attack_buffer.push(wanted, wanted_air, deadline))
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    ++buffered_presses;
#endif
                }
                // A full bounded buffer still owns this press: passing it to
                // native input as well would duplicate an already queued hit.
                return true;
            });
    }

    void CharacterCombatSwap::Impl::release_attack_press()
    {
        if (!attack_press_held)
        {
            return;
        }
        replaying_attack = true;
        try
        {
            if (auto* controller = attack_press_controller.Get();
                is_uobject_valid_fast(controller) &&
                object(object(controller, L"Player"), L"PlayerController") == controller)
            {
                runtime_controller_action_input(controller, L"attack", false);
            }
        }
        catch (...)
        {
            replaying_attack = false;
            attack_press_held = false;
            attack_press_controller = {};
            error = "kh02_buffered_attack_release";
            return;
        }
        replaying_attack = false;
        attack_press_held = false;
        attack_press_controller = {};
    }

    void CharacterCombatSwap::Impl::tick_attack_buffer(Clock::time_point now)
    {
        attack_buffer.expire(now);
        if (!attack_buffer.front() || queued)
        {
            return;
        }
        if (request_world.Get() != swap_context_world())
        {
            attack_buffer.clear();
            return;
        }
        auto* pawn = local_player_pawn(request_world.Get());
        auto* current = state(pawn);
        const int kind = state_id(current);
        if (!available(pawn) || (kind != player_state_ids::idle && kind != player_state_ids::run &&
                                 kind != player_state_ids::jump && !attack_state(current, pawn)))
        {
            attack_buffer.clear();
            return;
        }
        const auto attack_selected = attack_command_selected(pawn);
        if (!attack_selected)
        {
            // Keep only this unfinished, expiring intent until the new HUD
            // publishes its selection. Never replay against unknown UI.
            return;
        }
        if (!*attack_selected)
        {
            attack_buffer.clear();
            return;
        }
        if (attack_press_held)
        {
            return;
        }
        const auto intent = *attack_buffer.front();
        if (intent.attempted &&
            (now - intent.attempted_at < std::chrono::milliseconds(30) ||
             (attack_state(current, pawn) && (field<uint32_t>(current, 0x234) & 0x100u))))
        {
            return;
        }
        if (attack_state(current, pawn))
        {
            if (field<uint8_t>(current, combo_finisher_offset) && intent.index == 0)
            {
                return;
            }
            // Accept the press now; the native attack state still decides
            // when its next motion can start. Do not open its cancel gate.
            auto* anim = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
            Call input(anim, L"NotifyEnableInput");
            input.run();
        }
        auto* controller = object(pawn, L"Controller");
        attack_buffer.attempted(now);
        attack_press_controller = make_uobject_weak(controller);
        attack_press_held = true;
        attack_release_at = now + std::chrono::milliseconds(1);
        replaying_attack = true;
        try
        {
            runtime_controller_action_input(controller, L"attack", true);
        }
        catch (...)
        {
            replaying_attack = false;
            release_attack_press();
            throw;
        }
        replaying_attack = false;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        ++replayed_presses;
#endif
    }

    bool CharacterCombatSwap::Impl::swing_damage(UObject* victim, UObject* instigator,
                                                 UObject* causer, const void* event, bool record)
    {
        auto* owner = swing_owner.Get();
        if (GetCurrentThreadId() != game_thread || !is_uobject_valid_fast(victim) ||
            !is_uobject_valid_fast(owner) || actor_owning_world(owner) != swap_context_world() ||
            state(owner) != swing_state.Get())
        {
            return false;
        }
        // Match native challenge attribution, including weapon-caused hits.
        auto* controller = object(owner, L"Controller");
        if (causer != owner && (!is_uobject_valid_fast(controller) || instigator != controller))
        {
            return false;
        }
        const auto metadata = kh02_decode_damage_event(event);
        // Native normal sword hits use command 27. This includes elemental
        // normal finishers, while excluding lingering spells and shotlocks.
        if (!metadata || metadata->command != 27)
        {
            return false;
        }
        const auto weak = make_uobject_weak(victim);
        uint64_t identity{};
        static_assert(sizeof(weak) == sizeof(identity));
        std::memcpy(&identity, &weak, sizeof(identity));
        if (record)
        {
            swing_hits.record(identity);
            return false;
        }
        const bool blocked = swing_hits.blocked(identity);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (blocked)
        {
            ++duplicate_hits_blocked;
        }
#endif
        return blocked;
    }
} // namespace kh3ap
