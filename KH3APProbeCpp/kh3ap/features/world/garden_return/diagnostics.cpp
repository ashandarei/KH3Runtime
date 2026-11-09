#include "kh3ap/features/player/player_action_commands.h"
#include <chrono>
#include "service_impl.h"
#include "kh3ap/core/ui/ui_layout.h"
#include "kh3ap/features/world/garden_return/runtime_context.h"
#include "kh3ap/features/world/garden_return/alternate_interaction_service.h"
#include "kh3ap/features/world/garden_return/character_restoration.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/characters/character_layout.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/characters/character_unlocks.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/combat/character_shotlock.h"
#include "kh3ap/features/player/combat/character_style.h"
#include "kh3ap/features/player/combat/aqua_style_unlock.h"
#include "kh3ap/features/player/characters/character_pawn_pool.h"
#include "kh3ap/features/player/characters/character_swap_assets.h"
#include "kh3ap/features/player/characters/character_swap_effects.h"
#include "kh3ap/features/player/combat/kairi_shining_hud.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/movement/character_barrel_spin.h"
#include "kh3ap/features/ingredients/ingredient_tracker.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/core/integer.h"
#include <nlohmann/json.hpp>
#include "kh3ap/diagnostics/character_swap_trace.h"
#endif
#include "kh3ap/features/player/characters/kairi_idle.h"
#include "kh3ap/features/player/movement/alternate_wall_run.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/world/garden_return/garden_return_service.h"
#include "kh3ap/features/world/garden_return/garden_return_policy.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/session/command_guard.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/input/runtime_player_control.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/bridge/transport/request_file_watcher.h"
#endif
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/combat/kh02_combat_swap.h"
#include "kh3ap/features/kh02/combat/kh02_combo_master.h"
#include "kh3ap/features/kh02/combat/kh02_companion_handoff.h"
#include "kh3ap/features/kh02/combat/kh02_mickey_link.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_interaction_policy.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/events/world_context_events.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include <Unreal/UClass.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstring>
#include <sstream>
#include <vector>
#include <tuple>
#include "kh3ap/features/player/characters/character_swap_timing.h"

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
namespace kh3ap
{
    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_character_style(DiagnosticCommand& request)
    {
        auto* pawn = player();
        if (request.action == L"character_style_offer" &&
            !character_style::offer_for_test(pawn, static_cast<int>(
                JsonFieldReader::int64_field(request.text, "style_command").value_or(0))))
        {
            request.reason = L"ready_matching_character_required";
        }
        request.scope_probe = nlohmann::json::parse(character_style::inspect(pawn));
        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_cancel(DiagnosticCommand& request)
    {
        if (!ready(request.menu))
        {
            request.reason = L"ready_camp_required";
        }
        else if (Kh02Story::challenges_open())
        {
            Kh02Story::route_challenges(0, L"Escape", 0, false),
                Kh02Story::route_challenges(0, L"Escape", 1, false);
        }
        else
        {
            close_camp(request.menu);
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_experimental_pool(DiagnosticCommand& request)
    {
        if (!character_swap::swap_available(player()) ||
            !character_pawn_pool::set_enabled(
                JsonFieldReader::bool_field(request.text, "enabled").value_or(false)))
        {
            request.reason = L"character_pool_toggle_rejected";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_spellweaver_fixture(DiagnosticCommand& request)
    {
        auto* pawn = persistent_player_reward_context();
        if (!is_uobject_valid_fast(pawn) || pawn != player() || player_id(pawn) != aqua_player_id ||
            !loading_closed() || ready(request.menu) || save_open())
        {
            request.reason = L"ready_possessed_aqua_required";
        }
        else if (request.action == L"spellweaver_fixture_restore")
        {
            if (spellweaver_fixture_original)
            {
                aqua_style_unlock::publish(*spellweaver_fixture_original);
                spellweaver_fixture_original.reset();
            }
        }
        else if (const auto owned = JsonFieldReader::bool_field(request.text, "owned"))
        {
            if (!spellweaver_fixture_original)
            {
                spellweaver_fixture_original = aqua_style_unlock::owned();
            }
            aqua_style_unlock::publish(*owned);
        }
        else
        {
            request.reason = L"spellweaver_fixture_ownership_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_king_mickey_trap(DiagnosticCommand& request)
    {
        if (!character_swap::start_king_mickey_trap(player()))
        {
            request.reason = L"king_mickey_trap_deferred";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_experimental_select(DiagnosticCommand& request)
    {
        const auto choice = JsonFieldReader::int64_field(request.text, "player_id");
        if (!choice || !character_swap::diagnostic_select(
                           static_cast<int>(*choice),
                           JsonFieldReader::bool_field(request.text, "preload").value_or(true)))
        {
            request.reason = L"experimental_selection_rejected";
        }
        else if (JsonFieldReader::bool_field(request.text, "swap_immediately").value_or(false) &&
                 !combat_swap.request(player()))
        {
            request.reason = L"experimental_swap_rejected";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_alternate_wall_run_move(DiagnosticCommand& request)
    {
        const auto x = JsonFieldReader::int64_field(request.text, "x").value_or(0);
        const auto y = JsonFieldReader::int64_field(request.text, "y").value_or(0);
        if (x < -1 || x > 1 || y < -1 || y > 1 ||
            !alternate_wall_run::diagnostic_move(player(), static_cast<float>(x),
                                                 static_cast<float>(y)))
        {
            request.reason = L"ready_wall_run_movement_fixture_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_experimental_idle_battle(
        DiagnosticCommand& request)
    {
        if (!kairi_idle::diagnostic_battle(request.action == L"experimental_idle_battle"))
        {
            request.reason = L"ready_experimental_kairi_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_character_aim_race(DiagnosticCommand& request)
    {
        if (!character_shotlock::diagnostic_race_start(
                player(), checked_native_integer(
                              JsonFieldReader::int64_field(request.text, "order").value_or(-1))
                              .value_or(-1)))
        {
            request.reason = L"ready_character_aim_race_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_character_aim_scope(DiagnosticCommand& request)
    {
        request.scope_probe = character_shotlock::diagnostic_scope_mode(
            player(),
            checked_native_integer(JsonFieldReader::int64_field(request.text, "mode").value_or(-1))
                .value_or(-1));

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_experimental_swap(DiagnosticCommand& request)
    {
        if (!character_swap::route(0, L"Gamepad_RightTrigger", 0, true))
        {
            request.reason = L"experimental_swap_rejected";
        }
        else if (JsonFieldReader::bool_field(request.text, "capture_effect").value_or(false))
        {
            character_swap_effects::capture_next_for_test();
        }
        character_swap::route(0, L"Gamepad_RightTrigger", 1, true);
        if (request.reason.empty() &&
            JsonFieldReader::bool_field(request.text, "trace_frames").value_or(false))
        {
            character_swap_trace::arm();
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_challenges(DiagnosticCommand& request)
    {
        constexpr auto* key = L"Gamepad_LeftShoulder";
        if (!character_key(0, key, 0, true))
        {
            request.reason = L"ready_kh02_camp_required";
        }
        Kh02Story::route_challenges(0, key, 1, true);
        character_key(0, key, 1, true);

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_character_save(DiagnosticCommand& request)
    {
        if (!interactions.current_save_point(player()))
        {
            request.reason = L"active_aqua_save_prompt_required";
        }
        else
        {
            const auto* key =
                request.action == L"character_save_keyboard" ? L"F" : L"Gamepad_FaceButton_Top";
            if (!character_key(0, key, 0, request.action == L"character_save"))
            {
                request.reason = L"active_aqua_save_prompt_required";
            }
            character_key(0, key, 1, request.action == L"character_save");
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_mickey_link_offer(DiagnosticCommand& request)
    {
        if (!mickey_link.offer(player()))
        {
            request.reason = L"mickey_link_offer_rejected";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_ship_chart_cancel(DiagnosticCommand& request)
    {
        auto* pawn = player();
        auto* controller = object_field(pawn, L"Controller");
        auto* info = object_field(controller, L"m_pUICommandInfo");
        auto* actor = object_field(info, L"m_Actor");
        auto* component = object_field(info, L"m_Component");
        if (interactions.selected_ship_command(actor, component, pawn, false) !=
                player_action_command_value(PlayerActionCommand::CHECK) ||
            component != object_field(actor, L"TresReactorMap") || !loading_closed())
        {
            request.reason = L"owned_alternate_ship_chart_required";
        }
        else
        {
            runtime_controller_action_input(controller, L"command_cancel", true);
            phone_key_controller = make_uobject_weak(controller);
            phone_key_action = L"command_cancel";
            phone_key_id = request.id;
            phone_key_down = true;
            phone_key_release = Clock::now() + phone_button_hold_duration;
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_saved_alternate_return(DiagnosticCommand& request)
    {
        auto* pawn = player();
        const int saved_player = player_id(pawn);
        if (world().find(L"/Game/Maps/GardenOfAssemblage/") == std::wstring::npos ||
            (saved_player != player_id_value(PlayerId::Aqua) &&
             saved_player != player_id_value(PlayerId::Kairi) &&
             saved_player != player_id_value(PlayerId::Roxas)) ||
            !character_unlocks::available(saved_player) || character_swap::owns_swap_pawn(pawn) ||
            character_swap::manual_swaps_blocked() || !loading_closed() ||
            !runtime_sora_control_ready(pawn))
        {
            request.reason = L"unowned_saved_alternate_in_garden_required";
        }
        else
        {
            Kh02ReflectedCall paused(L"/Script/Engine.Default__GameplayStatics", L"IsGamePaused",
                                     "saved_alternate_return", max_camp_pause_parameter_bytes);
            paused.object(L"WorldContextObject", pawn);
            paused.run();
            if (paused.boolean_result())
            {
                request.reason = L"unpaused_saved_alternate_required";
            }
            else
            {
                Kh02ReflectedCall change(library(), L"BP_TresGameChangePlayer",
                                         "saved_alternate_return", max_camp_pause_parameter_bytes);
                change.object(L"WorldContextObject", pawn);
                change.integer(L"InKind", sora_player_id);
                change.run();
            }
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_capture_scene(DiagnosticCommand& request)
    {
        auto* pawn = player();
        if (!is_uobject_valid_fast(pawn) || !loading_closed())
        {
            request.reason = L"capture_requires_loaded_player";
        }
        else
        {
            Kh02ReflectedCall capture(L"/Script/Engine.Default__KismetSystemLibrary",
                                      L"ExecuteConsoleCommand", "character_capture",
                                      max_character_capture_parameter_bytes);
            capture.object(L"WorldContextObject", pawn);
            capture.string(L"Command",
                           JsonFieldReader::bool_field(request.text, "show_ui").value_or(true)
                               ? L"Shot showui"
                               : L"Shot");
            capture.run();
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_character_interact(DiagnosticCommand& request)
    {
        auto* pawn = player();
        auto* controller = object_field(pawn, L"Controller");
        auto* instance = object_field(viewport(), L"GameInstance");
        if ((character_swap::current_hero() != pawn &&
             (player_id(pawn) != aqua_player_id || !camp_is_kh02_world(world()))) ||
            !loading_closed() || ready(request.menu) || save_open() || !instance ||
            field<int32_t>(instance, game_instance_pause_type_offset) != 0)
        {
            request.reason = L"playable_owned_alternate_or_kh02_aqua_required";
        }
        else if (request.action == L"ship_chart_interact")
        {
            // Deterministic diagnostic selection of the inspected
            // chart when helm and chart ranges overlap. Gameplay still
            // uses its ordinary selected-reactor route above.
            auto* info = object_field(controller, L"m_pUICommandInfo");
            auto* ship = object_field(info, L"m_Actor");
            const auto requested_actor =
                JsonFieldReader::string_field(request.text, "actor").value_or(L"");
            if (!requested_actor.empty())
            {
                ship = nullptr;
                auto* ship_class = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, L"/Script/TresGame.TresVehiclePawnSpecialShip");
                for (auto* candidate :
                     find_live_actors_of_class(pawn, ship_class, max_leviathan_chart_candidates))
                {
                    if (candidate->GetPathName() == requested_actor)
                    {
                        if (ship)
                        {
                            throw std::runtime_error("ambiguous_ship_chart_actor");
                        }
                        ship = candidate;
                    }
                }
            }
            auto* reactor = object_field(ship, L"TresReactorMap");
            if (interactions.selected_ship_command(ship, reactor, pawn) != player_action_command_value(PlayerActionCommand::CHECK))
            {
                request.reason = L"nearby_active_ship_chart_required";
            }
            else
            {
                Kh02ReflectedCall select(controller, L"BP_NotifySetActionCommand1", "ship_chart",
                                         max_ship_chart_parameter_bytes);
                select.integer(L"inCommand", player_action_command_value(PlayerActionCommand::CHECK));
                select.number(L"inTimer", 1.0f);
                select.object(L"inActor", ship);
                select.object(L"InComponent", reactor);
                select.run();
                interactions.route_save_key(0, L"Gamepad_FaceButton_Top", 0);
                runtime_controller_action_input(controller, L"interact", true);
                save_press_controller = make_uobject_weak(controller);
                save_press_release = Clock::now() + save_button_hold_duration;
            }
        }
        else if (interactions.route_save_key(0, L"Gamepad_FaceButton_Top", 0))
        {
            interactions.route_save_key(0, L"Gamepad_FaceButton_Top", 1);
        }
        else
        {
            runtime_controller_action_input(controller, L"interact", true);
            save_press_controller = make_uobject_weak(controller);
            save_press_release = Clock::now() + save_button_hold_duration;
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_costume(DiagnosticCommand& request)
    {
        const auto* key =
            request.action == L"costume_open" || request.action == L"costume_category_next"
                ? L"Gamepad_RightShoulder"
            : request.action == L"costume_category_previous" ? L"Gamepad_LeftShoulder"
            : request.action == L"costume_next"              ? L"Gamepad_DPad_Down"
            : request.action == L"costume_previous"          ? L"Gamepad_DPad_Up"
            : request.action == L"costume_equip"             ? L"Gamepad_FaceButton_Bottom"
            : request.action == L"costume_color_more"        ? L"Gamepad_DPad_Right"
            : request.action == L"costume_color_less"        ? L"Gamepad_DPad_Left"
            : request.action == L"costume_color_fine_less"   ? L"Gamepad_FaceButton_Left"
            : request.action == L"costume_rotate_left"       ? L"Gamepad_RightStick_Left"
            : request.action == L"costume_rotate_right"      ? L"Gamepad_RightStick_Right"
                                                             : L"Gamepad_FaceButton_Right";
        if (request.action == L"costume_open")
        {
            if (!character_key(0, key, 0, true))
            {
                request.reason = L"ready_kh02_camp_required";
            }
            Kh02Story::route_challenges(0, key, 1, true);
            character_key(0, key, 1, true);
        }
        else
        {
            if (!Kh02Story::route_challenges(0, key, 0, true))
            {
                request.reason = L"open_dressing_room_required";
            }
            Kh02Story::route_challenges(0, key, 1, true);
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_character_aqua(DiagnosticCommand& request)
    {
        const auto* key =
            request.action == L"character_aqua" ? L"Gamepad_LeftTrigger" : L"Gamepad_RightTrigger";
        if (!character_key(0, key, 0, true))
        {
            request.reason = L"ready_kh02_camp_required";
        }
        character_key(0, key, 1, true);

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_combat_swap(DiagnosticCommand& request)
    {
        if (JsonFieldReader::bool_field(request.text, "trace_frames").value_or(false))
        {
            character_swap_trace::arm();
        }
        if (ready(request.menu) || !character_key(0, L"Gamepad_RightTrigger", 0, true))
        {
            request.reason = L"playable_kh02_combat_swap_required";
        }
        character_key(0, L"Gamepad_RightTrigger", 1, true);

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_combat_test_ground(DiagnosticCommand& request)
    {
        if (request.action == L"combat_test_menu")
        {
            if (!combat_swap.start_menu_test())
            {
                request.reason = L"playable_kh02_sora_menu_test_required";
            }
            return DiagnosticProgress::Finished;
        }
        const float phase = request.action == L"combat_test_early"  ? 0.2f
                            : request.action == L"combat_test_late" ? 0.7f
                                                                    : 0.35f;
        if (!combat_swap.start_test(request.action == L"combat_test_air",
                                    request.action == L"combat_test_expiry", phase))
        {
            request.reason = L"playable_kh02_combat_test_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_combat_test_camera(DiagnosticCommand& request)
    {
        if (!combat_swap.start_camera_test())
        {
            request.reason = L"playable_kh02_camera_test_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_character_effect_suppress(
        DiagnosticCommand& request)
    {
        const auto duration = JsonFieldReader::int64_field(request.text, "duration_ms").value_or(0);
        if (duration < 0 || duration > max_combat_test_duration_ms)
        {
            request.reason = L"effect_suppression_duration_invalid";
        }
        else
        {
            character_swap_effects::suppress_for_test(std::chrono::milliseconds{duration});
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_character_effect_preview(
        DiagnosticCommand& request)
    {
        if (!character_swap_effects::preview(player()))
        {
            request.reason = L"prepared_character_effect_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_combat_test_input(DiagnosticCommand& request)
    {
        if (!combat_swap.start_input_test(request.action == L"combat_test_input_swapback"))
        {
            request.reason = L"playable_kh02_input_test_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_combat_test_movement(DiagnosticCommand& request)
    {
        if (!combat_swap.start_movement_test(request.action == L"combat_test_continuity"))
        {
            request.reason = L"playable_kh02_movement_test_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_combat_test_locomotion(DiagnosticCommand& request)
    {
        const auto count = JsonFieldReader::int64_field(request.text, "swaps")
                               .value_or(default_locomotion_test_swaps);
        const auto warmup = JsonFieldReader::int64_field(request.text, "warmup_ms")
                                .value_or(default_locomotion_test_warmup_ms);
        const auto strength = JsonFieldReader::int64_field(request.text, "strength_percent")
                                  .value_or(default_locomotion_test_strength_percent);
        if (count < 0 || count > max_locomotion_test_swaps || warmup < 0 ||
            warmup > max_locomotion_test_warmup_ms || strength < 1 ||
            strength > maximum_locomotion_test_strength_percent ||
            !combat_swap.start_locomotion_test(
                static_cast<int>(count), static_cast<int>(warmup), static_cast<int>(strength),
                JsonFieldReader::bool_field(request.text, "release_at_swap").value_or(false),
                JsonFieldReader::bool_field(request.text, "camera_relative").value_or(false),
                JsonFieldReader::bool_field(request.text, "capture_gait").value_or(false)))
        {
            request.reason = L"ready_locomotion_test_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_combat_give_combo_master(
        DiagnosticCommand& request)
    {
        if (!combat_swap.give_combo_master())
        {
            request.reason = L"playable_sora_combo_master_grant_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_challenge_next(DiagnosticCommand& request)
    {
        if (!ready(request.menu) || !Kh02Story::challenges_open())
        {
            request.reason = L"challenge_log_required";
        }
        else
        {
            const auto* key = request.action == L"challenge_page"       ? L"PageDown"
                              : request.action == L"challenge_previous" ? L"Up"
                                                                        : L"Down";
            Kh02Story::route_challenges(0, key, 0, false);
            if (request.action == L"challenge_repeat")
            {
                Kh02Story::route_challenges(0, key, challenge_input_repeat_event, false);
            }
            Kh02Story::route_challenges(0, key, 1, false);
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_challenge_controller_hold_dpad(
        DiagnosticCommand& request)
    {
        if (!ready(request.menu) || !Kh02Story::challenges_open() || !challenge_hold_key.empty())
        {
            request.reason = L"idle_challenge_log_required";
        }
        else
        {
            challenge_hold_key =
                request.action == L"challenge_controller_hold_dpad"    ? L"Gamepad_DPad_Down"
                : request.action == L"challenge_controller_hold_stick" ? L"Gamepad_LeftStick_Down"
                : request.action == L"wardrobe_hold_left"              ? L"Gamepad_DPad_Left"
                : request.action == L"wardrobe_hold_right"             ? L"Gamepad_LeftStick_Right"
                                                                       : L"Left";
            challenge_hold_gamepad = request.action != L"wardrobe_hold_keyboard_left";
            challenge_hold_world = world();
            challenge_hold_release =
                Clock::now() + std::chrono::seconds(request.action.starts_with(L"wardrobe_")
                                                        ? wardrobe_request_timeout_seconds
                                                        : character_request_timeout_seconds);
            // A real press/release lifetime with deliberately no engine
            // Repeat events. The overlay's frame clock owns repeats.
            Kh02Story::route_challenges(0, challenge_hold_key, 0, challenge_hold_gamepad);
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_challenge_controller_burst(
        DiagnosticCommand& request)
    {
        if (!ready(request.menu) || !Kh02Story::challenges_open())
        {
            request.reason = L"challenge_log_required";
        }
        else
        {
            // Two aliases plus a same-frame repeat storm must be one
            // logical movement. The timing policy has clocked tests.
            Kh02Story::route_challenges(0, L"Gamepad_DPad_Down", 0, true);
            Kh02Story::route_challenges(0, L"Gamepad_LeftStick_Down", 0, true);
            for (int i = 0; i < max_challenge_scroll_releases; ++i)
            {
                Kh02Story::route_challenges(0, L"Gamepad_DPad_Down", challenge_input_repeat_event,
                                            true);
                Kh02Story::route_challenges(0, L"Gamepad_LeftStick_Down",
                                            challenge_input_repeat_event, true);
            }
            Kh02Story::route_challenges(0, L"Gamepad_DPad_Down", 1, true);
            Kh02Story::route_challenges(0, L"Gamepad_LeftStick_Down", 1, true);
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_open(DiagnosticCommand& request)
    {
        auto* manager = object_field(request.menu, L"m_pUIManager");
        auto* controller = object_field(manager, L"m_pPlayerController");
        auto* owner = library();
        auto* fn = owner ? owner->GetFunctionByNameInChain(L"TresUI_OpenCampMenu") : nullptr;
        if (!request.menu || field<uint8_t>(request.menu, native_ui::panel_open_offset) ||
            !eligible() || !controller)
        {
            request.reason = L"idle_world_required";
        }
        else if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != camp_latent_parameter_bytes ||
                 !find_function_param_by_name(fn, L"LatentInfo") ||
                 find_function_param_by_name(fn, L"LatentInfo")->GetOffset_Internal() !=
                     camp_latent_info_offset)
        {
            request.reason = L"open_layout_mismatch";
        }
        else
        {
            alignas(uint64_t) std::array<std::byte, camp_latent_parameter_bytes> params{};
            if (!write_object_function_param(fn, params.data(), L"WorldContextObject", controller))
            {
                request.reason = L"open_context_mismatch";
            }
            else
            {
                // No synthetic story callback. Diagnostic fixture only;
                // production never opens camp or changes its availability.
                const int32_t linkage = -1;
                std::memcpy(params.data() + camp_latent_info_offset, &linkage, sizeof(linkage));
                owner->ProcessEvent(fn, params.data());
            }
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_focus(DiagnosticCommand& request)
    {
        auto* root =
            object_field(object_field(request.menu, L"m_ASProxyUIComponent"), L"m_ASObject");
        if (!ready(request.menu) || !root || !button(request.menu) ||
            !try_gfx_invoke_int_arg(gfx, root, L"focusAnchor", garden_anchor))
        {
            request.reason = L"ready_camp_required";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_gummiphone(DiagnosticCommand& request)
    {
        const int row_index = request.action == L"inventory_equipment"   ? 0
                              : request.action == L"inventory_abilities" ? 1
                              : request.action == L"inventory_stock"     ? 3
                                                                         : 6;
        auto* row = stock(request.menu, row_index);
        const auto anchor = try_gfx_get_member_value_number(gfx, row, L"index");
        const auto parameter = try_gfx_get_member_value_number(gfx, row, L"userParam");
        const auto label = try_gfx_get_member_value_string(gfx, row, L"label");
        if (!ready(request.menu) || !row || anchor != camp_first_row_anchor + row_index ||
            parameter != row_index ||
            try_gfx_get_member_value(gfx, row, L"enabled").find(L"ASBoolean=true") ==
                std::wstring::npos)
        {
            request.reason = L"enabled_camp_row_required";
        }
        else
        {
            native_ui::InputEvent event{};
            event.set_sender(row);
            event.set_kind(native_ui::decide_event);
            event.set_anchor(camp_first_row_anchor + row_index);
            event.set_parameter(row_index);
            if (!reinterpret_cast<Input>(base + input_rva)(request.menu, event.data()) ||
                !event.handled())
            {
                request.reason = L"gummiphone_selection_not_accepted";
            }
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_gummiphone_inspect(DiagnosticCommand& request)
    {
        if (!gummiphone_diagnostic)
        {
            request.reason = L"gummiphone_diagnostic_unavailable";
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_gummiphone_confirm(DiagnosticCommand& request)
    {
        if (phone_key_completed != request.id)
        {
            if (phone_key_down)
            {
                if (phone_key_id == request.id)
                {
                    return DiagnosticProgress::Pending;
                }
                request.reason = L"gummiphone_key_pending";
            }
            else
            {
                auto* controller = object_field(object_field(request.menu, L"m_pUIManager"),
                                                L"m_pPlayerController");
                const auto snapshot = gummiphone_diagnostic ? gummiphone_diagnostic(-1) : "";
                const bool inventory_action = request.action.starts_with(L"inventory_");
                const bool combat_action = request.action.starts_with(L"combat_");
                const auto required = request.action == L"gummiphone_confirm"
                                          ? "\"home_focused\":true"
                                          : "\"focused\":true";
                if (!is_uobject_valid_fast(controller) || !loading_closed() ||
                    (combat_action ? (!combat_swap.available(player()) || combat_swap.busy())
                     : inventory_action
                         ? ((!camp_is_kh02_world(world()) && !character_swap::current_hero()) ||
                            (!field<uint8_t>(request.menu, native_ui::panel_open_offset) &&
                             (request.action == L"inventory_reload" ||
                              Kh02Story::persistent_reward_context() != player())))
                         : snapshot.find(required) == std::string::npos))
                {
                    request.reason = L"gummiphone_controller_or_focus_unavailable";
                }
                else
                {
                    try
                    {
                        phone_key_error.clear();
                        phone_key_action = request.action == L"combat_attack"      ? L"attack"
                                           : request.action == L"combat_jump"      ? L"jump"
                                           : request.action == L"inventory_down"   ? L"command_down"
                                           : request.action == L"inventory_up"     ? L"command_up"
                                           : request.action == L"inventory_reload" ? L"interact"
                                           : request.action == L"gummiphone_back" ||
                                                   request.action == L"inventory_back"
                                               ? L"command_cancel"
                                               : L"command_confirm";
                        phone_key_controller = make_uobject_weak(controller);
                        if (phone_key_controller.Get() != controller)
                        {
                            throw std::runtime_error("gummiphone_controller_weak_unavailable");
                        }
                        runtime_controller_action_input(controller, phone_key_action, true);
                        phone_key_id = request.id;
                        phone_key_down = true;
                        phone_key_release = Clock::now() + phone_button_hold_duration;
                        return DiagnosticProgress::Pending;
                    }
                    catch (const std::exception& error)
                    {
                        const std::string message = error.what();
                        request.reason = std::wstring(message.begin(), message.end());
                    }
                }
            }
        }
        else if (!phone_key_error.empty())
        {
            request.reason = phone_key_error;
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::diagnostic_select(DiagnosticCommand& request)
    {
        if (!ready(request.menu) || !eligible() || !button(request.menu))
        {
            request.reason = L"ready_camp_required";
        }
        else
        {
            native_ui::InputEvent event{};
            event.set_kind(native_ui::decide_event);
            event.set_anchor(garden_anchor);
            event.set_parameter(garden_anchor);
            reinterpret_cast<Input>(base + input_rva)(request.menu, event.data());
            if (!pending)
            {
                request.reason = L"selection_not_accepted";
            }
        }

        return DiagnosticProgress::Finished;
    }

    RuntimeGardenReturnService::Impl::DiagnosticProgress
    RuntimeGardenReturnService::Impl::dispatch_diagnostic(DiagnosticCommand& request)
    {
        struct Binding
        {
            std::wstring_view name;
            DiagnosticProgress (Impl::*handler)(DiagnosticCommand&);
            bool prefix;
        };
        static constexpr auto bindings = std::to_array<Binding>({
            {L"cancel", &Impl::diagnostic_cancel, false},
            {L"experimental_pool", &Impl::diagnostic_experimental_pool, false},
            {L"spellweaver_fixture", &Impl::diagnostic_spellweaver_fixture, false},
            {L"spellweaver_fixture_restore", &Impl::diagnostic_spellweaver_fixture, false},
            {L"character_style_inspect", &Impl::diagnostic_character_style, false},
            {L"character_style_offer", &Impl::diagnostic_character_style, false},
            {L"king_mickey_trap", &Impl::diagnostic_king_mickey_trap, false},
            {L"experimental_select", &Impl::diagnostic_experimental_select, false},
            {L"alternate_wall_run_move", &Impl::diagnostic_alternate_wall_run_move, false},
            {L"experimental_idle_battle", &Impl::diagnostic_experimental_idle_battle, false},
            {L"experimental_idle_relaxed", &Impl::diagnostic_experimental_idle_battle, false},
            {L"character_aim_race", &Impl::diagnostic_character_aim_race, false},
            {L"character_aim_scope", &Impl::diagnostic_character_aim_scope, false},
            {L"experimental_swap", &Impl::diagnostic_experimental_swap, false},
            {L"challenges", &Impl::diagnostic_challenges, false},
            {L"character_save", &Impl::diagnostic_character_save, false},
            {L"character_save_keyboard", &Impl::diagnostic_character_save, false},
            {L"mickey_link_offer", &Impl::diagnostic_mickey_link_offer, false},
            {L"ship_chart_cancel", &Impl::diagnostic_ship_chart_cancel, false},
            {L"saved_alternate_return", &Impl::diagnostic_saved_alternate_return, false},
            {L"capture_scene", &Impl::diagnostic_capture_scene, false},
            {L"character_interact", &Impl::diagnostic_character_interact, false},
            {L"ship_chart_interact", &Impl::diagnostic_character_interact, false},
            {L"costume_", &Impl::diagnostic_costume, true},
            {L"character_aqua", &Impl::diagnostic_character_aqua, false},
            {L"character_sora", &Impl::diagnostic_character_aqua, false},
            {L"combat_swap", &Impl::diagnostic_combat_swap, false},
            {L"combat_test_ground", &Impl::diagnostic_combat_test_ground, false},
            {L"combat_test_air", &Impl::diagnostic_combat_test_ground, false},
            {L"combat_test_expiry", &Impl::diagnostic_combat_test_ground, false},
            {L"combat_test_early", &Impl::diagnostic_combat_test_ground, false},
            {L"combat_test_late", &Impl::diagnostic_combat_test_ground, false},
            {L"combat_test_camera", &Impl::diagnostic_combat_test_camera, false},
            {L"character_effect_suppress", &Impl::diagnostic_character_effect_suppress, false},
            {L"character_effect_preview", &Impl::diagnostic_character_effect_preview, false},
            {L"combat_test_input", &Impl::diagnostic_combat_test_input, false},
            {L"combat_test_input_swapback", &Impl::diagnostic_combat_test_input, false},
            {L"combat_test_movement", &Impl::diagnostic_combat_test_movement, false},
            {L"combat_test_continuity", &Impl::diagnostic_combat_test_movement, false},
            {L"combat_test_locomotion", &Impl::diagnostic_combat_test_locomotion, false},
            {L"combat_give_combo_master", &Impl::diagnostic_combat_give_combo_master, false},
            {L"challenge_next", &Impl::diagnostic_challenge_next, false},
            {L"challenge_previous", &Impl::diagnostic_challenge_next, false},
            {L"challenge_page", &Impl::diagnostic_challenge_next, false},
            {L"challenge_repeat", &Impl::diagnostic_challenge_next, false},
            {L"challenge_controller_hold_dpad", &Impl::diagnostic_challenge_controller_hold_dpad,
             false},
            {L"challenge_controller_hold_stick", &Impl::diagnostic_challenge_controller_hold_dpad,
             false},
            {L"wardrobe_hold_left", &Impl::diagnostic_challenge_controller_hold_dpad, false},
            {L"wardrobe_hold_right", &Impl::diagnostic_challenge_controller_hold_dpad, false},
            {L"wardrobe_hold_keyboard_left", &Impl::diagnostic_challenge_controller_hold_dpad,
             false},
            {L"challenge_controller_burst", &Impl::diagnostic_challenge_controller_burst, false},
            {L"open", &Impl::diagnostic_open, false},
            {L"focus", &Impl::diagnostic_focus, false},
            {L"gummiphone", &Impl::diagnostic_gummiphone, false},
            {L"inventory_equipment", &Impl::diagnostic_gummiphone, false},
            {L"inventory_abilities", &Impl::diagnostic_gummiphone, false},
            {L"inventory_stock", &Impl::diagnostic_gummiphone, false},
            {L"gummiphone_inspect", &Impl::diagnostic_gummiphone_inspect, false},
            {L"gummiphone_navigate", &Impl::diagnostic_gummiphone_inspect, false},
            {L"gummiphone_confirm", &Impl::diagnostic_gummiphone_confirm, false},
            {L"gummiphone_back", &Impl::diagnostic_gummiphone_confirm, false},
            {L"inventory_confirm", &Impl::diagnostic_gummiphone_confirm, false},
            {L"inventory_back", &Impl::diagnostic_gummiphone_confirm, false},
            {L"combat_attack", &Impl::diagnostic_gummiphone_confirm, false},
            {L"combat_test_menu", &Impl::diagnostic_combat_test_ground, false},
            {L"combat_jump", &Impl::diagnostic_gummiphone_confirm, false},
            {L"inventory_down", &Impl::diagnostic_gummiphone_confirm, false},
            {L"inventory_up", &Impl::diagnostic_gummiphone_confirm, false},
            {L"inventory_reload", &Impl::diagnostic_gummiphone_confirm, false},
            {L"select", &Impl::diagnostic_select, false},
        });
        for (const auto& binding : bindings)
        {
            if (binding.prefix ? request.action.starts_with(binding.name)
                               : request.action == binding.name)
            {
                return (this->*binding.handler)(request);
            }
        }
        if (request.action != L"inspect")
        {
            request.reason = L"unknown_command";
        }
        return DiagnosticProgress::Finished;
    }

    void RuntimeGardenReturnService::Impl::diagnostic()
    {
        const auto directory = bridge();
        if (!directory)
        {
            return;
        }
        const auto path = *directory / L"runtime_garden_return_request.json";
        const auto text = read_text_file(path);
        if (!text)
        {
            return;
        }
        auto* menu = camp();
        const auto id = JsonFieldReader::string_field(*text, "request_id").value_or(L"");
        const auto action = JsonFieldReader::string_field(*text, "command").value_or(L"");
        nlohmann::json scope_probe;
        const auto expected = JsonFieldReader::string_field(*text, "expected_world").value_or(L"");
        std::wstring reason(
            runtime_command_request_failure(*text, GetCurrentProcessId(),
                                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count()));
        if (reason.empty() && (id.empty() || !JsonFieldReader::int64_field(*text, "process_id")))
        {
            reason = L"request_identity_required";
        }
        if (reason.empty() && action != L"inspect" && action != L"gummiphone_inspect" &&
            expected != world())
        {
            reason = L"expected_world_mismatch";
        }
        if (reason.empty())
        {
            DiagnosticCommand request{*text, menu, id, action, reason, scope_probe};
            if (dispatch_diagnostic(request) == DiagnosticProgress::Pending)
            {
                return;
            }
        }

        const auto interaction_snapshot = interactions.snapshot();
        const auto& restoration_snapshot = restoration.snapshot();
        std::ostringstream result;
        result << "{\"request_id\":\"" << json_escape(id)
               << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\""
               << (reason.empty() ? "completed" : "failed") << "\",\"reason\":\""
               << json_escape(reason) << "\",\"world\":\"" << json_escape(world())
               << "\",\"phase\":\"" << json_escape(phase)
               << "\",\"installed\":" << (installed ? "true" : "false")
               << ",\"ready\":" << (ready(menu) ? "true" : "false")
               << ",\"selections\":" << selections << ",\"last_kind\":" << last_kind
               << ",\"last_anchor\":" << last_anchor << ",\"last_parameter\":" << last_parameter
               << ",\"player_id\":" << player_id(player())
               << ",\"character_switches\":" << character_switches
               << ",\"pending_character\":" << pending_character
               << ",\"character_restore_checks\":" << restoration_snapshot.checks
               << ",\"character_restore_events\":" << restoration_snapshot.events
               << ",\"character_context_revision\":" << restoration_snapshot.context_revision
               << ",\"kh02_character_selected\":"
               << (restoration_snapshot.aqua_selected ? "true" : "false")
               << ",\"character_restore_pending\":"
               << (restoration_snapshot.pending ? "true" : "false") << ",\"player_pawn\":\""
               << json_escape(is_uobject_valid_fast(player()) ? player()->GetPathName() : L"")
               << "\",\"aqua_saves_opened\":" << interaction_snapshot.saves_opened
               << ",\"aqua_chest_pending\":"
               << ((interaction_snapshot.chest_observing || interactions.chest_pending()) ? "true"
                                                                                          : "false")
               << ",\"aqua_chests_opened\":" << interaction_snapshot.chests_opened
               << ",\"mirror_interaction_rejection\":\""
               << json_escape(interaction_snapshot.mirror_rejection) << "\""
               << ",\"spellweaver_owned\":" << (aqua_style_unlock::owned() ? "true" : "false")
               << ",\"aqua_collectible_pending\":"
               << (interaction_snapshot.collectible_pending ? "true" : "false")
               << ",\"aqua_collectibles_collected\":" << interaction_snapshot.collectibles_collected
               << ",\"rows\":[";
        bool comma = false;
        for (int row_index = 0; row_index < expanded_camp_row_count; ++row_index)
        {
            auto* row = row_index == stock_camp_row_count ? button(menu) : stock(menu, row_index);
            if (!row)
            {
                continue;
            }
            if (comma)
            {
                result << ',';
            }
            comma = true;
            result << "{";
            bool member_comma = false;
            for (const auto* key :
                 {L"name", L"label", L"index", L"userParam", L"x", L"y", L"width", L"height",
                  L"visible", L"enabled", L"selected", L"toUp", L"toDown"})
            {
                if (member_comma)
                {
                    result << ',';
                }
                member_comma = true;
                result << '"' << json_escape(key) << "\":\""
                       << json_escape(try_gfx_get_member_value(gfx, row, key)) << '"';
            }
            result << "}";
        }
        result
            << "],\"mickey_link\":" << mickey_link.snapshot() << ",\"companion_handoff\":\""
            << companion_handoff.status()
            << "\",\"combo_master\":" << combo_master.snapshot(player())
            << ",\"character_preload\":" << character_swap_assets::snapshot().dump()
            << ",\"character_effect\":" << character_swap_effects::snapshot().dump()
            << ",\"kairi_shining_hud\":" << kairi_shining_hud::snapshot().dump()
            << ",\"character_pool\":" << character_pawn_pool::snapshot().dump()
            << ",\"character_aim\":" << character_shotlock::snapshot(player()).dump()
            << ",\"character_scope_probe\":" << scope_probe.dump()
            << ",\"character_swap_trace\":" << character_swap_trace::snapshot().dump()
            << ",\"character_trap_locked\":"
            << (character_swap::manual_swaps_blocked() ? "true" : "false")
            << ",\"combat_swap\":" << combat_swap.snapshot() << ",\"gummiphone\":"
            << (gummiphone_diagnostic
                    ? gummiphone_diagnostic(
                          action == L"gummiphone_navigate"
                              ? checked_native_integer(
                                    JsonFieldReader::int64_field(*text, "navigation").value_or(-1))
                                    .value_or(-1)
                              : -1)
                    : "null")
            << "}\n";
        write_text_file_atomic_verified(*directory / L"runtime_garden_return_result.json",
                                        result.str());
        try_remove_file(path);
    }

} // namespace kh3ap
#endif
