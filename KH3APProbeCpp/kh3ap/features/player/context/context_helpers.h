#pragma once

#include "kh3ap/core/types.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UFunction;
    class UObject;
}

namespace kh3ap
{
    class RuntimeContextState;
    class RuntimeItemGrantPollState;
    class RuntimeObjectCache;
    class RuntimeState;

    auto read_runtime_context_snapshot(const std::optional<std::filesystem::path>& bridge_dir) -> std::optional<RuntimeContextSnapshot>;
    auto runtime_context_summary(
            const std::optional<RuntimeContextSnapshot>& current_snapshot,
            const std::optional<std::filesystem::path>& bridge_dir) -> std::wstring;
    auto runtime_context_summary(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> std::wstring;
    auto runtime_context_object_is_not_real(std::wstring_view context_summary) -> bool;
    auto runtime_context_object_is_not_real(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> bool;
    auto is_vbonus_probe_hook_name(
            std::wstring_view hook_name,
            std::wstring_view bp_get_vbonus_name,
            std::wstring_view tres_get_vbonus_name,
            std::wstring_view wait_bp_get_vbonus_name) -> bool;
    auto should_skip_vbonus_probe_for_runtime_context(
            const std::optional<RuntimeContextSnapshot>& current_snapshot) -> bool;
    auto derive_runtime_context_from_frame_full_name(
            std::wstring_view frame_full_name,
            std::wstring_view hook_name) -> std::optional<RuntimeContextSnapshot>;
    auto derive_runtime_context_from_object_full_name(
            std::wstring_view object_full_name,
            std::wstring_view hook_name) -> std::optional<RuntimeContextSnapshot>;
    auto write_runtime_context_file(
            const std::optional<std::filesystem::path>& bridge_dir,
            const RuntimeContextSnapshot& snapshot) -> void;
    auto publish_synthetic_runtime_context(
            std::wstring_view hook_name,
            std::wstring_view world,
            std::wstring_view outer,
            RuntimeContextState& context_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> void;
    auto maybe_publish_playable_runtime_context_from_cached_pawn(
            RC::Unreal::UObject* pawn,
            RuntimeContextState& context_state,
            RuntimeObjectCache& object_cache,
            const std::optional<std::filesystem::path>& bridge_dir,
            std::chrono::steady_clock::time_point now,
            std::wstring_view pawn_source = {},
            bool force_publish = false) -> void;
    auto maybe_publish_playable_runtime_context_from_cached_pawn(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir,
            std::chrono::steady_clock::time_point now,
            std::wstring_view pawn_source = {},
            bool force_publish = false) -> void;
    auto publish_goa_runtime_context(
            RC::Unreal::UObject* context,
            std::wstring_view hook_name,
            RuntimeContextState& context_state,
            RuntimeObjectCache& object_cache,
            RuntimeItemGrantPollState& item_grant_poll_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> void;
    auto publish_goa_runtime_context(
            RC::Unreal::UObject* context,
            std::wstring_view hook_name,
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> void;
    auto maybe_rearm_runtime_grant_for_ready_context(
            const RuntimeContextSnapshot& snapshot,
            const std::optional<std::filesystem::path>& request_path,
            std::chrono::steady_clock::duration runtime_item_grant_delay,
            RuntimeItemGrantPollState& item_grant_poll_state,
            const std::optional<RuntimeContextSnapshot>& current_snapshot,
            const std::optional<std::filesystem::path>& bridge_dir,
            const std::function<void()>& process_pending_runtime_item_grant) -> void;
    auto maybe_rearm_runtime_grant_for_ready_context(
            const RuntimeContextSnapshot& snapshot,
            const std::optional<std::filesystem::path>& request_path,
            std::chrono::steady_clock::duration runtime_item_grant_delay,
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir,
            const std::function<void()>& process_pending_runtime_item_grant) -> void;
    auto update_current_frame_context(
            RC::Unreal::UFunction* frame_node,
            std::wstring_view hook_name,
            RuntimeContextState& context_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> std::optional<RuntimeContextSnapshot>;
    auto update_current_frame_context(
            RC::Unreal::UFunction* frame_node,
            std::wstring_view hook_name,
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir) -> std::optional<RuntimeContextSnapshot>;
}
