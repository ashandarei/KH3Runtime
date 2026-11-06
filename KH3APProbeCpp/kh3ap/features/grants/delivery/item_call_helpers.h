#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    auto try_call_bp_tres_get_item(
            std::wstring_view runtime_token,
            RC::Unreal::UObject* world_context_object,
            bool show_info,
            RuntimeState& runtime_state,
            std::wstring_view bp_get_item_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_call_bp_tres_start_game_over(
            RC::Unreal::UObject* world_context_object,
            bool play_dead_se,
            RuntimeState& runtime_state,
            std::wstring_view bp_tres_start_game_over_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_call_bp_tres_have_item(
            std::wstring_view runtime_token,
            RC::Unreal::UObject* world_context_object,
            bool& has_item,
            RuntimeState& runtime_state,
            std::wstring_view bp_have_item_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_call_bp_tres_get_item_and_add_munny(
            std::wstring_view runtime_token,
            int32_t munny,
            RC::Unreal::UObject* world_context_object,
            bool show_info,
            RuntimeState& runtime_state,
            std::wstring_view bp_get_item_path,
            std::wstring_view bp_add_munny_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_call_bp_add_munny(
            int32_t munny,
            RuntimeState& runtime_state,
            std::wstring_view bp_add_munny_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_call_get_game_flag(
            std::wstring_view flag_name,
            int32_t& value,
            RuntimeState& runtime_state,
            std::wstring_view get_game_flag_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_call_get_game_flag_boolean(
            std::wstring_view flag_name,
            bool& value,
            RuntimeState& runtime_state,
            std::wstring_view get_game_flag_boolean_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_call_set_game_flag_boolean(
            std::wstring_view flag_name,
            bool value,
            RuntimeState& runtime_state,
            std::wstring_view set_game_flag_boolean_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_call_set_game_flag(
            std::wstring_view flag_name,
            int32_t value,
            RuntimeState& runtime_state,
            std::wstring_view set_game_flag_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason = nullptr) -> bool;
}
