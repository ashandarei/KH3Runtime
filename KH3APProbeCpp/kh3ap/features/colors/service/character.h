#pragma once

#include "kh3ap/features/colors/color_palette.h"

#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::color_service_detail
{
    [[nodiscard]] auto character_is_link(std::wstring_view character_key) -> bool;
    [[nodiscard]] auto character_is_toy(std::wstring_view character_key) -> bool;
    [[nodiscard]] auto character_is_creature(std::wstring_view character_key) -> bool;
    [[nodiscard]] auto resolve_supported_character_key(RC::Unreal::UObject* pawn)
        -> std::optional<std::wstring>;
    [[nodiscard]] auto resolve_world_name(RC::Unreal::UObject* object) -> std::wstring;
    [[nodiscard]] auto character_slot_gain(PaletteSlot slot) -> float;
    [[nodiscard]] auto character_color_for_slot(const CharacterPalette& palette, PaletteSlot slot,
                                                bool normalize) -> DerivedColor;
    [[nodiscard]] auto character_has_slot_plan(std::wstring_view character_key,
                                               std::wstring_view world_name) -> bool;
    auto apply_slot_plan_colors(RC::Unreal::UObject* component, std::wstring_view character_key,
                                std::wstring_view world_name, std::wstring_view seed,
                                RuntimeColorHarmonyMode mode) -> void;
    auto apply_character_colors(RC::Unreal::UObject* component, std::wstring_view character_key,
                                std::wstring_view seed, RuntimeColorHarmonyMode mode) -> void;
    auto log_link_readback(RC::Unreal::UObject* pawn, std::wstring_view character_key,
                           std::wstring_view tag) -> void;
    [[nodiscard]] auto
    resolve_skeletal_mesh_component(RC::Unreal::UObject* pawn, std::wstring_view character_key,
                                    bool log_full_dump, bool require_anim_instance)
        -> RC::Unreal::UObject*;
} // namespace kh3ap::color_service_detail
