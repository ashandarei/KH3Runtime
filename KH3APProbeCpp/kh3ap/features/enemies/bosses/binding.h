#pragma once

#include "kh3ap/features/enemies/remap_runtime.h"

namespace kh3ap::enemy_remap_detail
{
    [[nodiscard]] auto read_boss_owner_property(
            UObject* owner, const wchar_t* property_name) -> UObject*;

    [[nodiscard]] auto try_read_int32_object_property(
            UObject* owner,
            std::wstring_view property_name) -> std::optional<int32_t>;

    [[nodiscard]] auto has_runtime_boss_replacement_class(UObject* pawn)
            -> bool;

    [[nodiscard]] auto is_runtime_boss_replacement_pawn(UObject* pawn)
            -> bool;

    [[nodiscard]] auto resolve_and_bind_runtime_boss_pawn(UObject* pawn)
            -> RuntimeBossEntityBinding;

    [[nodiscard]] auto resolve_tracked_runtime_boss_pawn(UObject* pawn)
            -> RuntimeBossEntityBinding;

    [[nodiscard]] auto try_read_integral_param(
            UFunction* function,
            void* params,
            std::wstring_view param_name) -> std::optional<uint64_t>;

    [[nodiscard]] auto tracked_boss_source_class(
            std::wstring_view level_entity_path)
            -> std::optional<std::wstring>;
}
