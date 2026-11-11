#include <array>
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
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
            {L"inspect_emblems", &Impl::accept_inspect_emblems, false, true},
            {L"inspect_boss_setting", &Impl::accept_boss_setting, false, true},
            {L"set_boss_setting", &Impl::accept_boss_setting, true, true},
            {L"inspect", nullptr, false, false},
            {L"inspect_scene", &Impl::accept_inspect_scene, false, false},
            {L"inspect_transform", &Impl::accept_inspect_transform, false, true},
            {L"open_chest", &Impl::accept_open_chest, true, false},
            {L"cactuar_spawn", &Impl::accept_cactuar_spawn, true, false},
            {L"defeat_all", &Impl::accept_damage, true, false},
            {L"damage_pulse", &Impl::accept_damage, true, false},
            {L"kh02_boss_hp_setup", &Impl::accept_boss_hp_setup, true, false},
            {L"sora_protection", &Impl::accept_protection, true, false},
            {L"boss_center", &Impl::accept_boss_center, true, false},
            {L"xemnas_anchor", &Impl::accept_xemnas_anchor, true, false},
            {L"set_position", nullptr, true, false},
            {L"press_action", &Impl::accept_press_action, true, false},
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
    void RuntimeEnemyClearService::Impl::accept()
    {
        dir = config.bridge_dir();
        if (!dir || !path_points_to_regular_file(*dir / request_name))
        {
            return;
        }
        const auto text = read_text_file(*dir / request_name);
        if (!text)
        {
            return;
        }
        try_remove_file(*dir / request_name);
        request_id = JsonFieldReader::string_field(*text, "request_id").value_or(L"");
        if (request_id.empty() ||
            std::find(consumed.begin(), consumed.end(), request_id) != consumed.end())
        {
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
        position_before = position_after = position_immediate = requested_position = {};
        sora_hp_before = sora_hp_after = 0;
        world_name.clear();
        pawn_name.clear();
        started = Clock::now();
        command = JsonFieldReader::string_field(*text, "command").value_or(L"");
        expires = JsonFieldReader::int64_field(*text, "expires_unix_ms").value_or(0);
        expected_world = JsonFieldReader::string_field(*text, "expected_world").value_or(L"");
        if (JsonFieldReader::string_field(*text, "schema") != L"kh3ap.enemy_clear.v1" ||
            JsonFieldReader::int64_field(*text, "process_id") != GetCurrentProcessId() ||
            expires <= unix_ms() || expires > unix_ms() + max_enemy_clear_request_lifetime_ms)
        {
            finish(L"error", L"invalid_or_expired_identity");
            return;
        }
        scene_objects = "[]";
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
