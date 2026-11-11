#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>

#include <algorithm>
#include <stdexcept>

namespace kh3ap
{
    namespace
    {
        constexpr auto enemy_clear_update_budget = 4;
    }

    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    void RuntimeEnemyClearService::Impl::tick()
    {
        try
        {
            const auto now = Clock::now();
            if (photo_movement.Get() &&
                (now >= photo_hold_until || config.playable_sora() != photo_pawn.Get() ||
                 !held_camera(photo_pawn.Get())))
            {
                restore_photo_movement();
            }
            tick_protection();
            tick_test_center();
            tick_xemnas_anchor();
            mom_barrier.tick();
            if (update_camera_input(now))
            {
                return;
            }
            if (!chest_gates.empty() && now >= gate_next_poll)
            {
                gate_next_poll = now + battlegate_readiness_interval;
                auto* pawn = config.playable_sora();
                if (is_uobject_valid_fast(pawn) && cache_pawn.Get() != pawn && gameplay(pawn))
                {
                    ensure_cache_context(pawn);
                }
                for (auto& [id, gate] : chest_gates)
                {
                    if (gate.gate_enabled)
                    {
                        process_chest_gate_events(gate);
                    }
                }
            }
            if (update_combo_input(now))
            {
                return;
            }
            if (update_action_input(now))
            {
                return;
            }
            if (waiting && (command == L"open_chest" || command == L"press_action" ||
                            command == L"pooh_sign" || command == L"pooh_input"))
            {
                finish(L"error", L"interaction_controller_changed");
                return;
            }
            if (!waiting)
            {
                if (now < next_poll)
                {
                    return;
                }
                next_poll = now + enemy_clear_update_interval;
                if (requests.update(config.bridge_dir(), true))
                {
                    accept();
                }
                return;
            }
            // Expiry is checked before dispatching another target, even
            // when the game resumes after a long pause between ticks.
            if (unix_ms() >= expires)
            {
                finish(L"error", command == L"defeat_all"        ? L"defeat_not_observed"
                                 : command == L"break_container" ? L"container_reveal_not_observed"
                                                                 : L"position_observation_expired");
                return;
            }
            auto* pawn = request_pawn();
            if (!is_uobject_valid_fast(pawn) || sora.Get() != pawn ||
                actor_world(pawn) != world.Get())
            {
                finish(L"error", L"world_or_pawn_changed");
                return;
            }
            sora_hp_after = integer(pawn, L"GetHitPoint");
            if (command == L"break_emblem_cover")
            {
                auto* cover = container_ref.Get();
                if (!is_uobject_valid_fast(cover))
                {
                    throw std::runtime_error("emblem_cover_changed_before_break_proof");
                }
                container_destroyed = boolean(cover, L"BP_IsDestroyedGimmickForPlan");
                if (container_destroyed)
                {
                    finish(L"completed", L"native_emblem_cover_break_observed");
                }
                return;
            }
            if (command == L"break_container")
            {
                auto* container = container_ref.Get();
                auto* chest = container_chest_ref.Get();
                if (is_uobject_valid_fast(container))
                {
                    container_destroyed |= boolean(container, L"BP_IsDestroyedGimmickForPlan");
                }
                if (!is_uobject_valid_fast(chest))
                {
                    throw std::runtime_error("container_chest_changed");
                }
                chest_exposed = property_bool(chest, L"m_bIsActive", false) &&
                                !property_bool(chest, L"bHidden", false) &&
                                boolean(chest, L"GetActorEnableCollision");
                if (container_destroyed && chest_exposed)
                {
                    finish(L"completed", L"native_container_break_exposed_chest");
                }
                return;
            }
            if (command == L"defeat_all" || command == L"damage_pulse")
            {
                sample();
                if (next_target < targets.size())
                {
                    if (!gameplay(pawn))
                    {
                        finish(L"error", L"gameplay_blocked_before_defeat");
                        return;
                    }
                    size_t budget = enemy_clear_update_budget;
                    while (next_target < targets.size() && budget--)
                    {
                        auto& t = targets[next_target++];
                        auto* actor = t.actor.Get();
                        if (!t.proof.empty() || !is_uobject_valid_fast(actor))
                        {
                            continue;
                        }
                        if (sora.Get() != request_pawn() || actor_world(pawn) != world.Get() ||
                            !gameplay(pawn) ||
                            integer(actor, L"GetTeamNo") == integer(pawn, L"GetTeamNo"))
                        {
                            finish(L"error", L"target_context_changed");
                            return;
                        }
                        damage(t, actor, pawn);
                    }
                    sample();
                    // The accepted target checkpoint already preserves the
                    // full selection. Avoid rewriting it after each group
                    // of four; large clears otherwise generate quadratic IO.
                    if (next_target == targets.size())
                    {
                        pulse_observe_at = now + damage_pulse_observation_delay;
                        report(L"accepted", command == L"damage_pulse" ? L"damage_pulse_dispatched"
                                                                       : L"defeats_dispatched");
                    }
                }
                if (command == L"damage_pulse")
                {
                    if (next_target == targets.size() &&
                        (targets.empty() || now >= pulse_observe_at))
                    {
                        finish(L"completed", L"damage_pulse_observed");
                    }
                    return;
                }
                if (std::all_of(targets.begin(), targets.end(),
                                [](const Target& t)
                                {
                                    return !t.proof.empty();
                                }))
                {
                    finish(L"ready",
                           targets.empty() ? L"no_spawned_enemies" : L"native_defeats_observed");
                    return;
                }
            }
            else
            {
                position_after = position(pawn);
                // The immediate authoritative readback proved the write.
                // Report the next-frame position too: normal floor snapping
                // and trigger/physics movement are not teleport failures.
                finish(sora_hp_after > 0 ? L"ready" : L"error", sora_hp_after > 0
                                                                    ? L"position_write_observed"
                                                                    : L"sora_dead_after_teleport");
                return;
            }
        }
        catch (const std::exception& error)
        {
            const std::string reason = error.what();
            finish(L"error", std::wstring(reason.begin(), reason.end()));
        }
    }

} // namespace kh3ap
