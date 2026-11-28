#include <array>
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/AActor.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <sstream>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_enemy_clear_request_lifetime_ms = 120000;
    }

    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    const RuntimeEnemyClearService::Impl::RequestCommand*
    RuntimeEnemyClearService::Impl::find_request_command(std::wstring_view name) noexcept
    {
        static constexpr auto definitions = std::to_array<RequestCommand>({
            {L"set_photo_fov", &Impl::accept_photo_fov, true, true},
            {L"clear_photo_party", &Impl::accept_photo_party, true, true},
            {L"break_emblem_cover", &Impl::accept_break_emblem_cover, true, false},
            {L"hold_photo_position", &Impl::accept_photo_position, true, true},
            {L"look_camera", &Impl::accept_camera_look, true, true},
            {L"look_gameplay", &Impl::accept_camera_look, true, true},
            {L"move_gameplay", &Impl::accept_camera_look, true, true},
            {L"steer_gameplay", &Impl::accept_steer_gameplay, true, true},
            {L"inspect_emblems", &Impl::accept_inspect_emblems, false, true},
            {L"inspect_boss_setting", &Impl::accept_boss_setting, false, true},
            {L"set_boss_setting", &Impl::accept_boss_setting, true, true},
            {L"inspect", nullptr, false, false},
            {L"inspect_scene", &Impl::accept_inspect_scene, false, false},
            {L"inspect_talk", &Impl::accept_inspect_talk, true, true},
            {L"inspect_landmarks", &Impl::accept_inspect_landmarks, true, true},
            {L"inspect_airstep", &Impl::accept_inspect_airstep, true, true},
            {L"inspect_cover", &Impl::accept_inspect_cover, true, true},
            {L"inspect_transform", &Impl::accept_inspect_transform, false, true},
            {L"inspect_navigation", &Impl::accept_inspect_navigation, true, true},
            {L"open_chest", &Impl::accept_open_chest, true, false},
            {L"cactuar_spawn", &Impl::accept_cactuar_spawn, true, false},
            {L"olympus_statue_scene", &Impl::accept_olympus_statue_scene, true, false},
            {L"monstropolis_scream_scene", &Impl::accept_monstropolis_scream_scene, true, false},
            {L"defeat_all", &Impl::accept_damage, true, false},
            {L"damage_pulse", &Impl::accept_damage, true, false},
            {L"kh02_boss_hp_setup", &Impl::accept_boss_hp_setup, true, false},
            {L"sora_protection", &Impl::accept_protection, true, false},
            {L"boss_center", &Impl::accept_boss_center, true, false},
            {L"xemnas_anchor", &Impl::accept_xemnas_anchor, true, false},
            {L"tram_xion_fall", &Impl::accept_tram_xion_fall, true, false},
            {L"set_position", nullptr, true, false},
            {L"press_action", &Impl::accept_press_action, true, false},
            {L"gigas_blaster", &Impl::accept_gigas_blaster, true, false},
            {L"pooh_sign", &Impl::accept_pooh_sign, true, false},
            {L"attack_combo", &Impl::accept_attack_combo, true, false},
            {L"break_container", &Impl::accept_break_container, true, false},
            {L"chest_gate", &Impl::accept_chest_gate, true, false},
            {L"mom_barrier", &Impl::accept_mom_barrier, true, false},
            {L"repair_chest_record", &Impl::accept_repair_chest_record, true, false},
            {L"inspect_experience", &Impl::accept_experience, true, false},
            {L"award_experience", &Impl::accept_experience, true, false},
        });
        for (const auto& definition : definitions)
        {
            if (definition.name == name)
            {
                return &definition;
            }
        }
        return nullptr;
    }

    void RuntimeEnemyClearService::Impl::accept_cactuar_spawn(UObject* pawn, const std::string&)
    {
        // Diagnostics-only service: start the full authored appearance scene.
        // Calling Enemy_Appear alone leaves cinematic spawns intentionally hidden.
        if (world_name != L"/Game/Levels/ts/ts_02/ts_02.ts_02")
        {
            finish(L"error", L"toy_box_required");
            return;
        }
        enumerate(pawn);
        if (!targets.empty())
        {
            finish(L"error", L"clear_existing_enemies_first");
            return;
        }
        auto* script_class = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr,
            L"/Game/Levels/ts/ts_02/umap/ts_02_camera_ms141.ts_02_camera_ms141_C");
        if (!is_uobject_valid_fast(script_class))
        {
            finish(L"error", L"cactuar_enemy_level_not_loaded");
            return;
        }
        const auto scripts = actors_of_class(pawn, script_class);
        if (scripts.size() != 1 || !is_uobject_valid_fast(scripts.front()))
        {
            finish(L"error", L"cactuar_script_identity_ambiguous");
            return;
        }
        Call appearance(scripts.front(), L"ts_02_gameflow_ms140_Evt_ms141_Touch");
        appearance.invoke();
        finish(L"completed", L"appearance_event_dispatched_inspect_spawn_readback");
        return;
    }
    void RuntimeEnemyClearService::Impl::accept_olympus_statue_scene(UObject* pawn, const std::string&)
    {
        // Diagnostic replay of the real camera Blueprint entry, including its
        // statue animation and normal completion/skip callbacks. No guessed
        // graph offsets or synthetic scene completion.
        if (world_name != L"/Game/Levels/he/he_03/he_03.he_03")
        {
            finish(L"error", L"olympus_thebes_required");
            return;
        }
        if (!config.special_battle_mode_active || config.special_battle_mode_active())
        {
            finish(L"error", L"inactive_battle_required");
            return;
        }
        enumerate(pawn);
        if (!targets.empty())
        {
            finish(L"error", L"clear_existing_enemies_first");
            return;
        }
        // Enumerate from the permanent native base: StaticFindObject caches a
        // streamed Blueprint class by path and can return its old pointer after
        // this camera level has unloaded/reloaded for another replay.
        auto* script_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
            L"/Script/Engine.LevelScriptActor");
        if (!is_uobject_valid_fast(script_class))
        {
            finish(L"error", L"statue_camera_level_not_loaded");
            return;
        }
        auto scripts = actors_of_class(pawn, script_class);
        std::erase_if(scripts, [this](auto* script)
            {
                return !is_uobject_valid_fast(script) || actor_owning_world(script) != world.Get() ||
                    script->GetClassPrivate()->GetPathName() !=
                        L"/Game/Levels/he/he_03/umap/he_03_camera_ms027.he_03_camera_ms027_C";
            });
        if (scripts.size() != 1)
        {
            finish(L"error", L"statue_script_identity_ambiguous");
            return;
        }
        Call scene(scripts.front(), L"he_03_gameflow_REvt_Achilles_Start");
        scene.invoke();
        finish(L"completed", L"statue_scene_dispatched_observe_completion");
    }

    void RuntimeEnemyClearService::Impl::accept_monstropolis_scream_scene(UObject* pawn, const std::string&)
    {
        // Replay the owning story event, including streaming and its normal
        // movie/skip lifecycle. Never invoke an End/End_skip callback directly.
        if (world_name != L"/Game/Levels/mi/mi_04/mi_04.mi_04" ||
            !config.special_battle_mode_active || config.special_battle_mode_active())
        {
            finish(L"error", L"inactive_monstropolis_door_vault_required");
            return;
        }
        enumerate(pawn);
        if (!targets.empty())
        {
            finish(L"error", L"clear_existing_enemies_first");
            return;
        }
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
            L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        Call flag(library, L"GetGameFlag");
        if (!write_name_or_string_function_param(flag.field(L"FlagName"), flag.params.data(), L"gameflow_MI"))
        {
            throw std::runtime_error("monstropolis_flag_layout");
        }
        flag.invoke();
        auto* result = flag.field(L"ReturnValue");
        if (!result || result->GetClass().GetName() != L"IntProperty" || result->GetElementSize() != 4 ||
            *result->ContainerPtrToValuePtr<int32_t>(flag.params.data()) != 1250)
        {
            finish(L"error", L"completed_first_scream_strike_required");
            return;
        }
        auto* script_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
            L"/Script/Engine.LevelScriptActor");
        auto scripts = actors_of_class(pawn, script_class);
        std::erase_if(scripts, [this](auto* script)
            {
                return !is_uobject_valid_fast(script) || actor_owning_world(script) != world.Get() ||
                    script->GetClassPrivate()->GetPathName() !=
                        L"/Game/Levels/mi/mi_04/umap/mi_04_gameflow.mi_04_gameflow_C";
            });
        if (scripts.size() != 1)
        {
            finish(L"error", L"monstropolis_story_identity_ambiguous");
            return;
        }
        Call scene(scripts.front(), L"mi_04_gameflow_REvt_mi358");
        scene.invoke();
        finish(L"completed", L"scream_strike_scene_dispatched_observe_completion");
    }

    void RuntimeEnemyClearService::Impl::accept(
        std::optional<DiagnosticGameplayPipe::Request> fast_request)
    {
        pipe_ticket.reset();
        if (fast_request)
        {
            pipe_ticket = fast_request->ticket;
        }
        dir = config.bridge_dir();
        std::optional<std::string> text;
        if (fast_request)
        {
            text = std::move(fast_request->text);
        }
        else if (dir && path_points_to_regular_file(*dir / request_name))
        {
            text = read_text_file(*dir / request_name);
            if (text)
            {
                try_remove_file(*dir / request_name);
            }
        }
        if (!text)
        {
            return;
        }
        request_id = JsonFieldReader::string_field(*text, "request_id").value_or(L"");
        command = JsonFieldReader::string_field(*text, "command").value_or(L"");
        expires = JsonFieldReader::int64_field(*text, "expires_unix_ms").value_or(0);
        if (request_id.empty() ||
            std::find(consumed.begin(), consumed.end(), request_id) != consumed.end())
        {
            if (pipe_ticket)
            {
                finish(L"error", L"missing_or_replayed_request_id");
            }
            return;
        }
        consumed.push_back(request_id);
        if (consumed.size() > enemy_clear_request_history_limit)
        {
            consumed.pop_front();
        }
        targets.clear();
        spawn_points = "[]";
        sora = nullptr;
        world = nullptr;
        damage_percent = 0;
        pulse_observe_at = {};
        skipped_friendly = next_target = 0;
        position_dispatched = false;
        actor_rotation = {};
        transform_captured = false;
        movement_state = -1;
        wall_run_enabled.reset();
        moving_jump_left_ground = false;
        moving_on_ground.reset();
        movement_falling.reset();
        navigation_snapshot = "null";
        gigas_snapshot = "null";
        position_before = position_after = position_immediate = requested_position = {};
        sora_hp_before = sora_hp_after = 0;
        world_name.clear();
        pawn_name.clear();
        started = Clock::now();
        expected_world = JsonFieldReader::string_field(*text, "expected_world").value_or(L"");
        const auto lifetime = pipe_ticket ? DiagnosticGameplayPipe::max_request_lifetime_ms :
                                            max_enemy_clear_request_lifetime_ms;
        const auto now_ms = unix_ms();
        if (!dir || JsonFieldReader::string_field(*text, "schema") != L"kh3ap.enemy_clear.v1" ||
            JsonFieldReader::int64_field(*text, "process_id") != GetCurrentProcessId() ||
            expires <= now_ms || expires > now_ms + lifetime)
        {
            finish(L"error", L"invalid_or_expired_identity");
            return;
        }
        if (pipe_ticket && command != L"inspect" && command != L"inspect_transform" && command != L"inspect_navigation" &&
            command != L"inspect_scene" && command != L"move_gameplay" && command != L"steer_gameplay" &&
            command != L"look_gameplay" && command != L"press_action" && command != L"attack_combo" &&
            command != L"gigas_blaster" && command != L"inspect_cover" && command != L"inspect_talk" &&
            command != L"inspect_landmarks" && command != L"inspect_airstep")
        {
            finish(L"error", L"unsupported_diagnostics_pipe_command");
            return;
        }
        if (steer_until != Clock::time_point{} && command != L"steer_gameplay" &&
            command != L"inspect" && command != L"inspect_transform" && command != L"inspect_navigation" &&
            command != L"inspect_cover")
        {
            release_steer_input();
        }
        scene_objects = "[]";
        cover_snapshot = "null";
        in_native_cover = false;
        interaction_actor.clear();
        interaction_treasure.clear();
        interaction_dispatched = false;
        ingredient_counts = "{}";
        keyblade_ownership = "null";
        boss_setting = "null";
        boss_hp_setup_result = "null";
        experience_before = {};
        experience_after = {};
        experience_requested = 0;
        experience_dispatched = false;
        input_action.clear();
        input_dispatched = input_released = false;
        observing_talk = false;
        observing_airstep = airstep_guard_held = false;
        expected_airstep_actor.clear();
        input_controller = nullptr;
        waiting_dive_window = false;
        union_input_ride = nullptr;
        combo_active = combo_held = false;
        combo_controller = nullptr;
        combo_requested = combo_pressed = combo_released = combo_interval_ms = 0;
        combo_max_gap_ms = 0;
        container_ref = nullptr;
        container_chest_ref = nullptr;
        container_name.clear();
        break_dispatched = container_destroyed = chest_exposed = false;
        if (command == L"pooh_input")
        {
            accept_pooh_input(*text);
            return;
        }
        if (command == L"union_cross_input")
        {
            accept_union_cross_input(*text);
            return;
        }
        const auto* definition = find_request_command(command);
        if (!definition)
        {
            finish(L"error", L"unknown_command");
            return;
        }
        auto* pawn = request_pawn();
        if (!gameplay(pawn))
        {
            finish(L"error", L"controlled_combat_pawn_or_playable_sora_required");
            return;
        }
        sora = pawn;
        world = actor_world(pawn);
        if (sora.Get() != pawn || !is_uobject_valid_fast(world.Get()))
        {
            finish(L"error", L"weak_world_identity_unavailable");
            return;
        }
        world_name = world.Get()->GetPathName();
        pawn_name = pawn->GetPathName();
        const auto expected_pawn = JsonFieldReader::string_field(*text, "expected_pawn");
        if (expected_pawn && *expected_pawn != pawn_name)
        {
            finish(L"error", L"pawn_identity_mismatch");
            return;
        }
        if (definition->requires_world && (expected_world.empty() || expected_world != world_name))
        {
            finish(L"error", L"world_identity_mismatch");
            return;
        }
        position_before = position_after = position(pawn);

        if (definition->handler && definition->before_health)
        {
            (this->*definition->handler)(pawn, *text);
            return;
        }
        sora_hp_before = sora_hp_after = integer(pawn, L"GetHitPoint");

        if (command == L"inspect" || command == L"inspect_scene")
        {
            in_native_cover = integer(pawn, L"GetStateID") == player_state_ids::cover;
            inspect_gigas(pawn);
            // Read the owning runtime inventory, independently of AP delivery ledgers.
            std::ostringstream inventory;
            inventory << std::boolalpha << '{';
            bool first = true;
            for (const auto* key :
                 {L"WEP_KEYBLADE_SO_018", L"WEP_KEYBLADE_SO_01", L"WEP_KEYBLADE_SO_02",
                  L"WEP_KEYBLADE_SO_03", L"WEP_KEYBLADE_SO_04", L"WEP_KEYBLADE_SO_05",
                  L"WEP_KEYBLADE_SO_06", L"WEP_KEYBLADE_SO_07", L"WEP_KEYBLADE_SO_08",
                  L"WEP_KEYBLADE_SO_09", L"WEP_KEYBLADE_SO_011", L"WEP_KEYBLADE_SO_012"})
            {
                if (!first)
                {
                    inventory << ',';
                }
                first = false;
                inventory << '"' << utf8(key) << "\":" << keyblade_inventory(pawn, key);
            }
            inventory << '}';
            keyblade_ownership = inventory.str();
        }

        if (definition->handler)
        {
            (this->*definition->handler)(pawn, *text);
            return;
        }
        if (command == L"set_position")
        {
            constexpr std::array names{"x", "y", "z"};
            for (size_t i = 0; i < 3; ++i)
            {
                const auto value = JsonFieldReader::number_field(*text, names[i]);
                if (!value || !std::isfinite(*value) ||
                    std::abs(*value) > max_enemy_command_world_coordinate)
                {
                    finish(L"error", L"invalid_position");
                    return;
                }
                requested_position[i] = static_cast<float>(*value);
            }
            report(L"accepted", L"position_checkpoint");
            if (JsonFieldReader::bool_field(*text, "gigas").value_or(false))
            {
                set_gigas_position(pawn);
                finish(L"completed", L"gigas_position_observed");
                return;
            }
            set_position(pawn);
        }
        else
        {
            enumerate(pawn);
            if (command == L"inspect")
            {
                finish(L"completed", L"enemy_snapshot");
                return;
            }
            report(L"accepted", L"target_checkpoint");
        }
        waiting = true;
    }

} // namespace kh3ap
