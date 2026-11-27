#include <polyhook2/Detour/x64Detour.hpp>
#include "swap_impl.h"
#include "swap_state.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/state/player_state_queue.h"
#include "kh3ap/features/player/characters/character_camera.h"
#include "kh3ap/features/player/characters/character_pose_handoff.h"
#include "kh3ap/features/player/characters/character_idle.h"
#include "kh3ap/features/player/combat/character_shotlock.h"
#include "kh3ap/core/events/world_context_events.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/kh02/world/kh02_tunnel_mirrors.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/TArray.hpp>
#include <algorithm>

namespace kh3ap::combat_swap_detail
{
    using namespace RC::Unreal;

    UObject* swap_context_world()
    {
        if (auto* world = kh02_current_story_world())
        {
            return world;
        }
        return character_swap::enabled() ? kh02_current_world() : nullptr;
    }

    static bool item_use_clear(UObject* pawn)
    {
        const auto* queue = read_player_state_queue(object(pawn, L"MyStateComp"));
        if (!queue)
        {
            return false;
        }
        for (auto* queued_state : *queue)
        {
            if (state_id(queued_state) == player_state_ids::use_item)
            {
                return false;
            }
        }
        return true;
    }

    static bool ordinary_state(int id)
    {
        // Idle, Run, Jump (including falling), supported normal attacks,
        // Guard, DodgeRoll and RiskDodge. Scripted/special states retain R2.
        return id == player_state_ids::idle || id == player_state_ids::run ||
               id == player_state_ids::jump || id == player_state_ids::attack ||
               id == player_state_ids::attack_aqua || id == player_state_ids::attack_riku ||
               id == player_state_ids::attack_roxas || id == player_state_ids::attack_kairi ||
               id == player_state_ids::guard || id == player_state_ids::dodge_roll ||
               id == player_state_ids::risk_dodge;
    }
} // namespace kh3ap::combat_swap_detail

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace combat_swap_detail;

    const uintptr_t sora_selection_rva =
        character_native::rva(character_native::Site::sora_selection);
    const uintptr_t aqua_selection_rva =
        character_native::rva(character_native::Site::aqua_selection);
    const uintptr_t attack_enter_rva = character_native::rva(character_native::Site::attack_enter);
    const uintptr_t attack_prepare_rva =
        character_native::rva(character_native::Site::attack_prepare);

    CharacterCombatSwap::Impl::Impl(Player get_player, Change change_player)
        : player(std::move(get_player)), change(std::move(change_player))
    {
    }

    CharacterCombatSwap::Impl::~Impl()
    {
        release_attack_press();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        session.release_test_input();
#endif
        remove_hooks();
    }

    void CharacterCombatSwap::Impl::remove_hooks()
    {
        auto remove = [](const std::unique_ptr<PLH::x64Detour>& hook)
        {
            if (hook && hook->isHooked())
            {
                hook->unHook();
            }
        };
        remove(sora_hook);
        remove(aqua_hook);
        remove(run_hook);
        remove(camera_hook);
        remove(attack_hook);
        remove(prepare_hook);
        for (const auto& hook : alternate_hooks)
        {
            remove(hook);
        }
        if (active == this)
        {
            active = nullptr;
        }
    }

    bool CharacterCombatSwap::Impl::install()
    {
        if (attempted)
        {
            return installed;
        }
        attempted = true;
        // Reuse the existing shared animation-play hook for every hero's
        // first pose; do not stack a second detour on the same entry.
        if (!character_idle::initialize())
        {
            error = "character_swap_animation_hook";
            return false;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!character_native::verify_sites(
                {character_native::Site::sora_selection, character_native::Site::aqua_selection,
                 character_native::Site::attack_enter, character_native::Site::attack_prepare,
                 character_native::Site::queue_clear, character_native::Site::queue_push,
                 character_native::Site::queue_update, character_native::Site::run_enter,
                 character_native::Site::camera_update, character_native::Site::riku_selection,
                 character_native::Site::kairi_selection, character_native::Site::roxas_selection,
                 character_native::Site::camera_target_init,
                 character_native::Site::air_jump_available,
                 character_native::Site::set_air_jump_available,
                 character_native::Site::skeletal_tick_animation,
                 character_native::Site::skeletal_refresh_bones}))
        {
            error = "kh02_combat_swap_build_profile";
            return false;
        }
        const std::array<uintptr_t, 3> selectors{
            character_native::rva(character_native::Site::riku_selection),
            character_native::rva(character_native::Site::kairi_selection),
            character_native::rva(character_native::Site::roxas_selection)};
        active = this;
        game_thread = GetCurrentThreadId();
        sora_hook = std::make_unique<PLH::x64Detour>(
            base + sora_selection_rva, reinterpret_cast<uintptr_t>(&select_sora), &original_sora);
        aqua_hook = std::make_unique<PLH::x64Detour>(
            base + aqua_selection_rva, reinterpret_cast<uintptr_t>(&select_aqua), &original_aqua);
        run_hook = std::make_unique<PLH::x64Detour>(
            base + character_native::rva(character_native::Site::run_enter),
            reinterpret_cast<uintptr_t>(&enter_run), &original_run);
        camera_hook = std::make_unique<PLH::x64Detour>(
            base + character_native::rva(character_native::Site::camera_update),
            reinterpret_cast<uintptr_t>(&update_camera), &original_camera);
        attack_hook = std::make_unique<PLH::x64Detour>(
            base + attack_enter_rva, reinterpret_cast<uintptr_t>(&enter_attack), &original_attack);
        prepare_hook = std::make_unique<PLH::x64Detour>(
            base + attack_prepare_rva, reinterpret_cast<uintptr_t>(&prepare_attack),
            &original_prepare);
        const std::array<Select, 3> callbacks{&select_riku, &select_kairi, &select_roxas};
        for (size_t index = 0; index < selectors.size(); ++index)
        {
            alternate_hooks[index] = std::make_unique<PLH::x64Detour>(
                base + selectors[index], reinterpret_cast<uintptr_t>(callbacks[index]),
                &original_alternates[index]);
        }
        installed = sora_hook->hook() && original_sora && aqua_hook->hook() && original_aqua &&
                    run_hook->hook() && original_run && camera_hook->hook() && original_camera &&
                    attack_hook->hook() && original_attack && prepare_hook->hook() &&
                    original_prepare;
        for (size_t index = 0; index < alternate_hooks.size() && installed; ++index)
        {
            installed = alternate_hooks[index]->hook() && original_alternates[index];
        }
        if (!installed)
        {
            remove_hooks();
            error = "kh02_combat_swap_selection_hook";
        }
        return installed;
    }

    bool CharacterCombatSwap::Impl::available(UObject* pawn)
    {
        const int id = player_id(pawn);
        if (!kh02_current_story_world())
        {
            return character_swap::swap_available(pawn) &&
                   (ordinary_state(state_id(state(pawn))) ||
                    state_id(state(pawn)) == player_state_ids::glide ||
                    state_id(state(pawn)) == player_state_ids::floating);
        }
        if ((id != player_id_value(PlayerId::Sora) && id != player_id_value(PlayerId::Aqua)) ||
            !Kh02Story::native_save_ready(pawn) ||
            actor_owning_world(pawn) != swap_context_world() || !runtime_sora_control_ready(pawn) ||
            Kh02Story::challenges_open() || !ordinary_state(state_id(state(pawn))))
        {
            return false;
        }
        Call paused(L"/Script/Engine.Default__GameplayStatics", L"IsGamePaused");
        paused.object(L"WorldContextObject", pawn);
        paused.run();
        return !paused.boolean_result();
    }

    bool CharacterCombatSwap::Impl::request(UObject* pawn)
    {
        if (queued || !available(pawn) || !install())
        {
            return false;
        }
        int captured_index = -1;
        bool captured_air = false;
        auto* current = state(pawn);
        if (attack_state(current, pawn) && !field<uint8_t>(current, combo_finisher_offset))
        {
            const int index = field<int>(current, combo_index_offset);
            if (index >= 0 && index < 64)
            {
                captured_index = index + 1;
                captured_air = field<uint8_t>(current, combo_air_offset) != 0;
            }
        }
        else if (incoming.Get() == pawn && Clock::now() <= combo_until)
        {
            captured_index = next_index;
            captured_air = air;
        }
        capture_frame(pawn);
        outgoing = make_uobject_weak(pawn);
        request_world = make_uobject_weak(actor_owning_world(pawn));
        incoming = {};
        positioned = {};
        next_index = captured_index;
        air = captured_air;
        target = kh02_current_story_world() ? (player_id(pawn) == player_id_value(PlayerId::Sora)
                                                   ? player_id_value(PlayerId::Aqua)
                                                   : player_id_value(PlayerId::Sora))
                                            : character_swap::swap_target(pawn);
        requested_at = Clock::now();
        waiting_for_preload = false;
        attack_input_until = requested_at + switch_timeout + combo_grace;
        combo_until = {};
        if (!kh02_current_story_world() && !character_shotlock::begin_swap_input_check(pawn))
        {
            return false;
        }
        queued = true;
        dispatched = false;
        input_seeded = false;
        pose_pending = true;
        run_pose_pending = false;
        error.clear();
        return true;
    }

    auto CharacterCombatSwap::Impl::tick(Clock::time_point now) -> std::optional<int>
    {
        complete_first_movement(now);
        if (attack_press_held && now >= attack_release_at)
        {
            release_attack_press();
        }
        attack_buffer.expire(now);
        if (!queued)
        {
            character_shotlock::finish_swap_input_check();
            tick_attack_buffer(now);
            if (next_index >= 0 &&
                (now > combo_until || player() != incoming.Get() ||
                 request_world.Get() != swap_context_world() || !available(incoming.Get())))
            {
                next_index = -1;
                incoming = {};
            }
            return std::nullopt;
        }
        // Keep the existing weak transaction and capture movement only at
        // dispatch. Waiting after the factory request would preserve an old
        // position/camera sample and consume the two-second handoff deadline.
        const bool preload_pending =
            !kh02_current_story_world() && character_swap::preparation_pending(target);
        if (!dispatched && request_world.Get() == swap_context_world() &&
            player() == outgoing.Get() && preload_pending &&
            now - requested_at < std::chrono::seconds(30))
        {
            waiting_for_preload = true;
            return std::nullopt;
        }
        if (waiting_for_preload && !dispatched && !preload_pending)
        {
            if (!kh02_current_story_world() &&
                !character_shotlock::begin_swap_input_check(player()))
            {
                queued = false;
                attack_buffer.clear();
                error = "kh02_combat_swap_input_context";
                return std::nullopt;
            }
            requested_at = now;
            attack_input_until = now + switch_timeout + combo_grace;
            next_index = -1;
            auto* current = state(player());
            if (attack_state(current, player()) && !field<uint8_t>(current, combo_finisher_offset))
            {
                const int current_index = field<int>(current, combo_index_offset);
                if (current_index >= 0 && current_index < 64)
                {
                    next_index = current_index + 1;
                    air = field<uint8_t>(current, combo_air_offset) != 0;
                }
            }
            waiting_for_preload = false;
        }
        if (request_world.Get() != swap_context_world() || now - requested_at > switch_timeout)
        {
            character_shotlock::finish_swap_input_check();
            queued = false;
            next_index = -1;
            attack_buffer.clear();
            error = "kh02_combat_swap_context_or_timeout";
            return std::nullopt;
        }
        auto* pawn = player();
        if (!dispatched)
        {
            // The item can already belong to the outgoing pawn's queue while
            // its active state is still Idle or Jump. Let it apply in place.
            if (pawn != outgoing.Get() || !available(pawn) || !item_use_clear(pawn))
            {
                character_shotlock::finish_swap_input_check();
                queued = false;
                next_index = -1;
                attack_buffer.clear();
                attack_input_until = {};
                error = "kh02_combat_swap_dispatch_rejected";
                return std::nullopt;
            }
            if (!kh02_current_story_world())
            {
                const auto input_status = character_shotlock::swap_input_status(pawn);
                if (input_status == character_shotlock::SwapInputStatus::Waiting)
                {
                    return std::nullopt;
                }
                if (input_status == character_shotlock::SwapInputStatus::Blocked)
                {
                    character_shotlock::finish_swap_input_check();
                    queued = false;
                    next_index = -1;
                    attack_buffer.clear();
                    attack_input_until = {};
                    error = "kh02_combat_swap_aim_input_rejected";
                    return std::nullopt;
                }
            }
            capture_frame(pawn);
            if (target == static_cast<int>(PlayerId::KingMickey))
            {
                swing_pending = false;
                next_index = -1;
                attack_buffer.clear();
            }
            release_attack_press();
            attack_buffer.handoff();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (session.test_movement)
            {
                session.release_test_input();
            }
#endif
            if (!change(pawn, target))
            {
                character_shotlock::finish_swap_input_check();
                queued = false;
                next_index = -1;
                attack_buffer.clear();
                attack_input_until = {};
                error = "kh02_combat_swap_dispatch_rejected";
                return std::nullopt;
            }
            character_shotlock::finish_swap_input_check();
            dispatched = true;
        }
        // The local player can render its newly possessed pawn before the
        // UI/save context and normal gameplay state have caught up. Apply
        // the visual handoff there immediately, before those later gates.
        pawn = local_player_pawn(request_world.Get());
        if (player_id(pawn) != target || pawn == outgoing.Get())
        {
            return std::nullopt;
        }
        const character_pose_handoff::Scope pose_scope(
            object(object(pawn, L"MyMesh"), L"AnimScriptInstance"));
        auto* controller = object(pawn, L"Controller");
        if (!input_seeded && is_uobject_valid_fast(object(controller, L"PlayerInput")))
        {
            runtime_controller_restore_stick(controller, false, stick_input);
            runtime_controller_restore_stick(controller, true, look_input);
            input_seeded = true;
        }
        const bool position_pending = positioned.Get() != pawn;
        if (position_pending && !position_incoming_pawn(pawn))
        {
            return std::nullopt;
        }
        if (input_seeded)
        {
            pose_pending = resume_run(pawn) || pose_pending;
        }
        if (pose_pending)
        {
            refresh_handoff_pose(pawn);
            // Measure the current pose, never a pooled pawn's parked pose.
            character_camera::match_field_height(pawn);
            pose_pending = false;
        }
        if (position_pending)
        {
            // A parked action's exit can reset camera interpolation. Seed
            // the source view only after that cleanup and pose evaluation,
            // so returning to a grounded hero cannot revive its old dive.
            restore_camera(pawn, control_rotation, rendered_view, rendered_timestamp, zoom, orbit);
            transition_camera =
                make_uobject_weak(object(camera_manager(pawn), L"m_CameraNormal1st"));
            camera_transition_frames = 2;
        }
        if (!input_seeded || pawn != player() || !available(pawn))
        {
            return std::nullopt;
        }
        incoming = make_uobject_weak(pawn);
        kh02_refresh_tunnel_mirrors_after_swap(pawn);
        combo_until = std::max(combo_until, now + combo_grace);
        attack_input_until = combo_until;
        queued = false;
        tick_attack_buffer(now);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        ++swaps;
#endif
        return target;
    }

    CharacterCombatSwap::CharacterCombatSwap(Player player, Change change)
        : player(std::move(player)), change(std::move(change))
    {
    }

    CharacterCombatSwap::~CharacterCombatSwap() = default;

    bool CharacterCombatSwap::initialize()
    {
        if (!impl && swap_context_world())
        {
            impl = std::make_unique<Impl>(player, change);
        }
        return impl != nullptr;
    }

    bool CharacterCombatSwap::request(UObject* pawn, bool trap_owned)
    {
        try
        {
            if (character_swap::manual_swaps_blocked() && !trap_owned)
            {
                return false;
            }
            return initialize() && impl->request(pawn);
        }
        catch (const std::exception& failure)
        {
            if (impl)
            {
                impl->error = failure.what();
            }
            return false;
        }
    }

    bool CharacterCombatSwap::available(UObject* pawn) const
    {
        try
        {
            return Impl::available(pawn);
        }
        catch (...)
        {
            return false;
        }
    }

    bool CharacterCombatSwap::busy() const
    {
        return impl && impl->queued;
    }

    bool CharacterCombatSwap::attack_key(int controller, std::wstring_view key, int event)
    {
        try
        {
            return impl && impl->attack_key(controller, key, event);
        }
        catch (const std::exception& failure)
        {
            impl->error = failure.what();
            return false;
        }
    }

    auto CharacterCombatSwap::tick(Clock::time_point now) -> std::optional<int>
    {
        if (!impl)
        {
            initialization.observe(world_context_revision.load(std::memory_order_acquire));
            if (!initialization.due(now))
            {
                return std::nullopt;
            }
            const bool settled = !swap_context_world() || initialize();
            initialization.complete(settled, now);
        }
        if (!impl)
        {
            return std::nullopt;
        }
        try
        {
            const auto result = impl->tick(now);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            impl->session.tick_test(now);
#endif
            return result;
        }
        catch (const std::exception& failure)
        {
            impl->error = failure.what();
            impl->queued = false;
            impl->next_index = -1;
            impl->attack_buffer.clear();
            impl->release_attack_press();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            impl->session.release_test_input();
            runtime_controller_test_stick_lease(nullptr, false);
            impl->session.test_phase = 0;
            impl->session.test_status = "failed_exception";
#endif
            return std::nullopt;
        }
    }

    bool CharacterCombatSwap::blocks_swing_damage(UObject* victim, UObject* instigator,
                                                  UObject* causer, const void* event)
    {
        try
        {
            return Impl::active &&
                   Impl::active->swing_damage(victim, instigator, causer, event, false);
        }
        catch (...)
        {
            return false;
        }
    }

    void CharacterCombatSwap::record_swing_damage(UObject* victim, UObject* instigator,
                                                  UObject* causer, const void* event)
    {
        try
        {
            if (Impl::active)
            {
                Impl::active->swing_damage(victim, instigator, causer, event, true);
            }
        }
        catch (...)
        {
        }
    }
} // namespace kh3ap
