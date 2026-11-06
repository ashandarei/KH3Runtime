#pragma once

#include "kh3ap/features/enemies/remap_runtime.h"

namespace kh3ap::enemy_remap_detail
{
    auto register_level_entity_wipe_out_groups(
            UObject* level_entity,
            std::wstring_view level_entity_path) -> void;

    auto prepare_runtime_boss_state(
            UnrealScriptFunctionCallableContext& callback_context,
            void*) -> void;

    auto observe_runtime_boss_state(
            UnrealScriptFunctionCallableContext& callback_context,
            void*) -> void;

    auto observe_or_suppress_runtime_boss_wipe_out(
            UnrealScriptFunctionCallableContext& callback_context,
            void*) -> void;
}
