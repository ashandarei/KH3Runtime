#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    inline constexpr std::wstring_view data_xemnas_class =
        L"/Game/Blueprints/Enemy/e_ex353/e_ex353_Pawn.e_ex353_Pawn_C";

    // The native GetCenterActor reads this slot from the current level entity.
    auto data_xemnas_center_slot(RC::Unreal::UObject* entity) -> RC::Unreal::UObject**;
    auto data_xemnas_center_actor(RC::Unreal::UObject* pawn) -> std::optional<RC::Unreal::UObject*>;
    auto data_xemnas_center_location(RC::Unreal::UObject* pawn)
        -> std::optional<std::array<float, 3>>;

    namespace enemy_remap_detail
    {
        void prepare_runtime_xemnas_center(RC::Unreal::UObject* pawn);
        void retire_runtime_xemnas_center(RC::Unreal::UObject* pawn);
    } // namespace enemy_remap_detail
} // namespace kh3ap
