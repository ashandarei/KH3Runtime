#pragma once

#include "kh3ap/features/enemies/remap_runtime.h"

namespace kh3ap::enemy_remap_detail
{
    auto install_runtime_boss_phase_guard() -> void;
    auto install_grim_guardianess_mode_guard() -> void;

    auto initialize_runtime_larxene_phase(UObject* pawn) -> void;

    auto install_kg_replacement_reveal_guard() -> void;

    auto install_opening_ansem_phase_driver() -> void;
    auto read_boss_gameflow_flag(std::wstring_view flag_name) -> std::optional<int64_t>;

    auto prepare_marshmallow_replacement_cutscene(
            UnrealScriptFunctionCallableContext& callback_context,
            void*) -> void;

    auto process_pending_marshmallow_cutscene() -> void;

    auto skip_marshmallow_replacement_cutscene(
            UnrealScriptFunctionCallableContext&, void*) -> void;

    auto install_terranort_controller_guard() -> void;

    auto install_boss_death_partner_guard() -> void;
}
