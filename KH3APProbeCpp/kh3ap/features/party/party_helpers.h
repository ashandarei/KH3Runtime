#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
    class UFunction;
} // namespace RC::Unreal

namespace kh3ap::party_native
{
    // Read only the requested authoritative weak slots; never scan world actors.
    auto read_friend_slots(RC::Unreal::UObject* manager, std::span<RC::Unreal::UObject*> slots)
        -> bool;

    auto is_friend_component(RC::Unreal::UObject* object) -> bool;

    // Game-thread helpers. Returned objects are validated local observations;
    // callers retaining an object across frames must use a weak reference.
    auto read_object_like_property(RC::Unreal::UObject* object, std::wstring_view property_name)
        -> RC::Unreal::UObject*;
    auto write_object_like_property(RC::Unreal::UObject* object, std::wstring_view property_name,
                                    RC::Unreal::UObject* value) -> bool;
    auto run_behavior_tree(RC::Unreal::UObject* controller, RC::Unreal::UObject* behavior_tree)
        -> bool;
    auto persistent_level_for(RC::Unreal::UObject* object) -> RC::Unreal::UObject*;
    auto configure_party_navigation(RC::Unreal::UObject* pawn, RC::Unreal::UObject* controller)
        -> bool;
    auto resolve_named_function(RC::Unreal::UObject* object, std::wstring_view function_name)
        -> RC::Unreal::UFunction*;

    // The named getter validates the complete parameter list. The path getter
    // retains its existing ReturnValue-based contract; these are distinct APIs.
    auto invoke_named_bool_getter(RC::Unreal::UObject* object, std::wstring_view function_name)
        -> std::optional<bool>;
    auto invoke_named_single_bool_function(RC::Unreal::UObject* object,
                                           std::wstring_view function_name, bool value) -> bool;
    auto invoke_bool_getter(RC::Unreal::UObject* object, std::wstring_view function_path)
        -> std::optional<bool>;
    auto invoke_set_disable_battle_mode_ai(RC::Unreal::UObject* pawn, bool disabled) -> bool;

    auto actor_position(RC::Unreal::UObject* object) -> std::optional<std::array<float, 3>>;
    auto outside_native_follow_distance(const std::array<float, 3>& first,
                                        const std::array<float, 3>& second) -> bool;
} // namespace kh3ap::party_native
