#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    void RuntimeEnemyClearService::Impl::report(std::wstring_view status, std::wstring_view reason)
    {
        if (!dir) return;
        std::ostringstream out;
        out << std::boolalpha << "{\"schema\":\"kh3ap.enemy_clear.v1\",\"request_id\":\"" << json_escape(request_id)
            << "\",\"command\":\"" << json_escape(command) << "\",\"process_id\":" << GetCurrentProcessId()
            << ",\"expires_unix_ms\":" << expires << ",\"status\":\"" << json_escape(status)
            << "\",\"reason\":\"" << json_escape(reason) << "\",\"at\":\"" << now_iso8601_utc()
            << "\",\"build\":\"" << __DATE__ << ' ' << __TIME__ << "\",\"world\":\"" << json_escape(world_name)
            << "\",\"pawn\":\"" << json_escape(pawn_name) << "\",\"elapsed_ms\":"
            << std::chrono::duration<double, std::milli>(Clock::now() - started).count()
            << ",\"sora_hp_before\":" << sora_hp_before << ",\"sora_hp_after\":" << sora_hp_after
            << ",\"damage_percent\":" << damage_percent
            << ",\"mutation_context_ready\":" << mutation_context_ready
            << ",\"mutation_control_ready\":" << mutation_control_ready
            << ",\"special_battle_mode_active\":"
            << (config.special_battle_mode_active
                    ? (config.special_battle_mode_active() ? "true" : "false") : "null")
            << ",\"protection_active\":" << (protection_expires > unix_ms() && is_uobject_valid_fast(protected_sora.Get()))
            << ",\"protection_expires_unix_ms\":" << protection_expires
            << ",\"opening_protection_armed\":" << (opening_protection_expires>unix_ms())
            << ",\"protection_no_damage_response\":" << protection_no_damage_response
            << ",\"protection_reason\":\"" << json_escape(protection_reason) << "\""
            << ",\"center_actor\":\"" << json_escape(center_actor) << "\""
            << ",\"center_active\":" << (center_expires>unix_ms() && is_uobject_valid_fast(centered_boss.Get()))
            << ",\"center_expires_unix_ms\":" << center_expires;
        out << ",\"center_before\":";json_position(out,center_before);
        out << ",\"center_after\":";json_position(out,center_after);
        out
            << ",\"skipped_friendly\":" << skipped_friendly << ",\"position_before\":";
        json_position(out, position_before);
        out << ",\"transform_captured\":" << transform_captured;
        out << ",\"rotation_pitch_yaw_roll_degrees\":"; json_position(out, actor_rotation);
        out << ",\"position_after\":"; json_position(out, position_after);
        out << ",\"position_immediate\":"; json_position(out, position_immediate);
        out << ",\"requested_position\":"; json_position(out, requested_position);
        out << ",\"position_dispatched\":" << position_dispatched << ",\"targets\":[";
        size_t proven = 0;
        for (size_t i = 0; i < targets.size(); ++i)
        {
            const auto& t = targets[i];
            if (i) out << ',';
            if (!t.proof.empty()) ++proven;
            out << "{\"actor\":\"" << json_escape(t.name) << "\",\"class\":\"" << json_escape(t.class_name)
                << "\",\"entity\":\"" << json_escape(t.entity_name) << "\",\"hp_before\":" << t.hp_before
                << ",\"hp_after\":" << t.hp_after << ",\"max_hp\":" << t.max_hp << ",\"state_before\":" << t.state_before
                << ",\"state_after\":" << t.state_after << ",\"team\":" << t.team
                << ",\"kills_before\":" << t.kills_before << ",\"kills_after\":" << t.kills_after
                << ",\"requested\":" << t.requested << ",\"death_observed\":" << t.death_observed
                << ",\"vanished\":" << t.vanished << ",\"invincible\":" << t.invincible
                << ",\"hp_floor\":" << t.hp_floor << ",\"no_damage\":" << t.no_damage
                << ",\"damage_returned\":" << t.damage_returned << ",\"damage_requested\":" << t.damage_requested
                << ",\"proof\":\"" << json_escape(t.proof) << "\",\"position\":";
            json_position(out, t.location); out << '}';
        }
        out << "],\"spawn_points\":" << spawn_points << ",\"photo_position_held\":" << (photo_movement.Get()!=nullptr)
            << ",\"camera\":" << camera_snapshot << ",\"emblems\":" << emblems << ",\"scene_objects\":" << scene_objects << ",\"ingredient_counts\":" << ingredient_counts << ",\"chest_gate\":" << gate_snapshot << ",\"chest_gates\":" << gates_snapshot
            << ",\"mom_barrier\":" << mom_snapshot
            << ",\"keyblade_ownership\":" << keyblade_ownership
            << ",\"boss_setting\":" << boss_setting
            << ",\"boss_hp_setup\":" << boss_hp_setup_result
            << ",\"xemnas_anchor\":" << anchor_snapshot
            << ",\"level_before\":" << experience_before.level << ",\"level_after\":" << experience_after.level
            << ",\"exp_before\":" << experience_before.total_exp << ",\"exp_after\":" << experience_after.total_exp
            << ",\"exp_requested\":" << experience_requested << ",\"exp_dispatched\":" << experience_dispatched
            << ",\"input_action\":\"" << json_escape(input_action) << "\",\"input_dispatched\":" << input_dispatched
            << ",\"input_released\":" << input_released
            << ",\"combo_requested\":" << combo_requested << ",\"combo_pressed\":" << combo_pressed
            << ",\"combo_released\":" << combo_released << ",\"combo_interval_ms\":" << combo_interval_ms
            << ",\"combo_max_gap_ms\":" << combo_max_gap_ms
            << ",\"container_actor\":\"" << json_escape(container_name) << "\",\"break_dispatched\":" << break_dispatched
            << ",\"container_destroyed\":" << container_destroyed << ",\"chest_exposed\":" << chest_exposed
            << ",\"interaction_dispatched\":" << interaction_dispatched
            << ",\"interaction_actor\":\"" << json_escape(interaction_actor)
            << "\",\"interaction_treasure\":\"" << json_escape(interaction_treasure)
            << "\",\"target_count\":" << targets.size() << ",\"defeated_count\":" << proven
            << ",\"all_defeated\":" << (proven == targets.size()) << "}\n";
        write_text_file_atomic_verified(*dir / result_name, out.str());
        append_text_file(*dir / events_name, out.str());
    }

    void RuntimeEnemyClearService::Impl::finish(std::wstring_view status, std::wstring_view reason)
    {
        if(combo_held) {
            if(auto* c=combo_controller.Get();is_uobject_valid_fast(c)) {
                runtime_controller_action_input(c,input_action,false);++combo_released;
            }
            combo_held=false;
        }
        if(combo_active)input_released=combo_pressed==combo_released;
        combo_active=false;combo_controller=nullptr;
        waiting=false;report(status,reason);
    }

}
