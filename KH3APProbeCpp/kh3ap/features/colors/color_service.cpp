#include "kh3ap/app/config.h"
#include "kh3ap/features/colors/color_service.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/colors/service/service_impl.h"
#include "kh3ap/features/colors/service/character.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/diagnostics/ui_diagnostic_state.h"
#include "kh3ap/features/colors/color_events.h"
#include "kh3ap/features/colors/color_object_registry.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/characters/character_swap.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <atomic>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace color_service_detail;

    namespace
    {
        using ::kh3ap::settings::s_debug_logging;
    }

    RuntimeColorService::RuntimeColorService(RuntimeColorServiceConfig config)
        : impl_(std::make_unique<Impl>(config))
    {
    }

    RuntimeColorService::~RuntimeColorService() = default;
    RuntimeColorService::RuntimeColorService(RuntimeColorService&&) noexcept = default;
    auto RuntimeColorService::operator=(RuntimeColorService&&) noexcept
        -> RuntimeColorService& = default;

    auto RuntimeColorService::install_once() -> void
    {
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Runtime color service installed\n"));
    }

    auto RuntimeColorService::on_sora_state(UObject* pawn) -> void
    {
        if (impl_->enabled())
        {
            impl_->observe_active_link(pawn, std::chrono::steady_clock::now());
        }
    }

    auto RuntimeColorService::on_pawn_possessed(UObject* pawn) -> void
    {
        if (!impl_->enabled())
        {
            return;
        }
        // Covers AI-controlled companions (Donald/Goofy spawn through the
        // same SetFriend->Possess path as the custom party AI pawns).
        // Sora's own PlayerController possession does not fire this native
        // hook at all - see the pawn-state check in tick() below.
        impl_->maybe_queue(pawn, std::chrono::steady_clock::now());
    }

    auto RuntimeColorService::on_map_jump() -> void
    {
        impl_->state.reset_for_map_jump();
        impl_->mesh_dump_logged_.clear();
        impl_->last_attempt_at_.clear();
        impl_->pawn_generation_ = ~uint64_t{0};
        impl_->pawn_discovery_pending_ = true;
        impl_->weapons_dirty_ = true;
        impl_->gummi_.on_map_jump();
        impl_->link_repaints_.clear();
    }

    auto RuntimeColorService::on_character_swap() -> void
    {
        if (!impl_->enabled())
        {
            return;
        }
        // A pooled pawn has no new creation event. Check only the committed,
        // locally owned incoming pawn; already-colored pawns remain untouched.
        // Equipment rebuilds have their own revision, including fresh weapons
        // on a reused pawn, and do not require another character discovery.
        impl_->maybe_queue(character_swap::party_owner(), std::chrono::steady_clock::now());
        impl_->alternate_context_retry_at_ = {};
    }

    auto RuntimeColorService::reroll_colors() -> void
    {
        // UE4SS input callbacks run outside the game thread.
        impl_->reroll_requested_.store(true, std::memory_order_relaxed);
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeColorService::test_gummi_snapshot() const -> std::string
    {
        return impl_->gummi_.test_snapshot();
    }
#endif

    auto RuntimeColorService::tick(std::chrono::steady_clock::time_point now) -> void
    {
        color_objects::initialize();
        // Picked up before the enabled gate below, so switching the
        // in-game AP Options row takes effect on the characters already
        // standing there instead of waiting for the next map load.
        if (const auto mode = impl_->current_mode(); mode != impl_->last_mode_)
        {
            const auto previous = impl_->last_mode_;
            impl_->last_mode_ = mode;
            if (previous.has_value())
            {
                // Remove the old mode's overrides before applying the new
                // one, including parameters Coordinated deliberately skips.
                impl_->restore_all_colored(now);
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Color mode changed {} -> {}\n"),
                                               color_randomization_mode_id(*previous),
                                               color_randomization_mode_id(mode));
            }
        }

        // The whole feature off. Checked here rather than at every call
        // site so there is one place that decides, and no pawn is queued
        // in the first place.
        if (!impl_->enabled())
        {
            impl_->reroll_requested_.store(false, std::memory_order_relaxed);
            return;
        }
        if (impl_->reroll_requested_.exchange(false, std::memory_order_relaxed))
        {
            impl_->apply_reroll(now);
        }
        // Ahead of every Sora-based gate below: the world map and the Gummi
        // routes have no Sora at all.
        impl_->gummi_.tick(impl_->seed_identity(), impl_->harmony_mode(), now);
        impl_->state.prune_expired();
        std::erase_if(impl_->mesh_dump_logged_,
                      [](const FWeakObjectPtr& weak)
                      {
                          return !is_uobject_valid_fast(weak.Get());
                      });
        std::erase_if(impl_->last_attempt_at_,
                      [](const auto& entry)
                      {
                          return !is_uobject_valid_fast(entry.first.Get());
                      });
        const auto equipment_generation =
            color_equipment_generation.load(std::memory_order_acquire);
        if (equipment_generation != impl_->equipment_generation_)
        {
            impl_->equipment_generation_ = equipment_generation;
            impl_->weapons_dirty_ = true;
        }
        auto& pending = impl_->state.pending();
        if (character_swap::handoff_pending())
        {
            return;
        }

        auto& runtime_state = impl_->config.runtime_state;
        auto& pawn_state = runtime_state.grant_state().pawn_state();
        const auto& blocking_ui = runtime_state.ui_diagnostic_state().blocking_ui_state();
        const auto pawn_candidates =
            collect_cached_runtime_grant_pawn_candidates(runtime_state, nullptr);
        const auto safety_status =
            runtime_grant_playable_safety_status(blocking_ui, pawn_state, pawn_candidates, now);
        bool color_ready = runtime_color_safety_ready(safety_status.ready, safety_status.reason);
        UObject* reference_pawn = color_ready && pawn_state.candidate_trusted()
                                      ? pawn_state.object()
                                      : nullptr;
        if (!color_ready && now >= impl_->alternate_context_retry_at_ &&
            (!pending.empty() || !impl_->link_repaints_.empty() ||
             (impl_->weapons_dirty_ && now >= impl_->weapon_retry_at_) ||
             impl_->pawn_discovery_pending_ ||
             color_objects::generation() != impl_->pawn_generation_) &&
            current_runtime_blocking_ui_status(blocking_ui).ready)
        {
            // Verify possession and loading/control readiness only while color work is pending.
            impl_->alternate_context_retry_at_ = now + Impl::apply_retry_interval;
            reference_pawn = character_swap::reward_context();
            color_ready = reference_pawn != nullptr;
        }
        if (reference_pawn)
        {
            impl_->discover_pawns(reference_pawn, now);
        }
        if (color_ready)
        {
            impl_->repaint_links(now);
        }

        if (pending.empty())
        {
            if (color_ready)
            {
                impl_->color_equipped_weapons(now);
            }
            return;
        }
        std::erase_if(pending,
                      [](const RuntimeColorPendingPawn& entry)
                      {
                          return !entry.pawn.Get() || !is_uobject_valid_fast(entry.pawn.Get());
                      });
        if (pending.empty())
        {
            impl_->last_blocked_reason_.clear();
            return;
        }
        if (!color_ready)
        {
            // Pawns are waiting and the gate is refusing. Without this the
            // two cases look identical from the log - a pawn that was never
            // detected prints nothing, and a pawn detected but held back by
            // a cutscene also prints nothing - yet they need opposite fixes.
            // Printed once per distinct reason, so a scene that blocks for
            // minutes costs one line.
            if (s_debug_logging && safety_status.reason != impl_->last_blocked_reason_)
            {
                impl_->last_blocked_reason_ = safety_status.reason;
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Color apply blocked reason={} details={} pending={}\n"),
                    safety_status.reason, safety_status.details,
                    static_cast<uint64_t>(pending.size()));
            }
            return;
        }
        impl_->last_blocked_reason_.clear();

        std::erase_if(
            pending,
            [this, now](const RuntimeColorPendingPawn& entry)
            {
                if (impl_->state.is_colored(entry.pawn.Get()))
                {
                    return true;
                }
                // Re-check map validity right before touching the pawn: a new
                // transition may have started in the ticks since this entry was
                // queued.
                if (!impl_->world_is_allowed(entry.pawn.Get(), now))
                {
                    return false;
                }
                if (!impl_->ready_to_attempt(entry.pawn.Get(), now))
                {
                    return false;
                }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                const auto apply_started = std::chrono::steady_clock::now();
#endif
                if (impl_->try_apply(entry, now))
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    const auto applied_at = std::chrono::steady_clock::now();
                    Output::send<LogLevel::Normal>(
                        STR("[KH3APProbeCpp] Color timing key={} queued_to_apply_ms={} "
                            "world_seen_to_apply_ms={} apply_ms={}\n"),
                        entry.character_key,
                        std::chrono::duration<double, std::milli>(applied_at - entry.queued_at)
                            .count(),
                        std::chrono::duration<double, std::milli>(applied_at -
                                                                  impl_->world_first_seen_at_)
                            .count(),
                        std::chrono::duration<double, std::milli>(applied_at - apply_started)
                            .count());
#endif
                    impl_->state.mark_colored(entry.pawn.Get());
                    impl_->weapons_dirty_ = true;
                    if (character_is_link(entry.character_key))
                    {
                        log_link_readback(entry.pawn.Get(), entry.character_key,
                                          STR("after_apply"));
                        impl_->link_repaints_.push_back({entry.pawn, entry.character_key,
                                                         now + Impl::link_repaint_after[0], 0});
                    }
                    return true;
                }
                return false;
            });
        impl_->color_equipped_weapons(now);
    }
} // namespace kh3ap
