#pragma once

#include "kh3ap/features/enemies/remap_runtime.h"

namespace kh3ap::enemy_remap_detail
{
    [[nodiscard]] auto resolve_loaded_level_entity(
            const RuntimeLoadedLevelEntityRemap& remap) -> UObject*;

    auto queue_loaded_toggle_class_prewarm_paths() -> uint32_t;

    auto process_one_pending_class_prewarm() -> bool;

    void process_pending_class_rebinds();

    [[nodiscard]] auto reconcile_loaded_level_entity_remaps(
            RuntimeLevelEntityRemapKind kind,
            bool enabled) -> RuntimeLevelEntityReconcileResult;

    auto install_level_entity_serialize_detour(UObject* exemplar) -> bool;
    void install_cactuar_replacement_reveal_guard();
}
