#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    struct RuntimeHookFilterConfig
    {
        bool enable_runtime_reward_diagnostics{false};
        bool enable_bonus_trace_logging{false};
        bool enable_stable_ability_trace_hooks{false};
        std::vector<std::wstring> utility_reward_frame_names{};
        std::vector<std::wstring> diagnostic_hook_names{};
        std::vector<std::wstring> post_load_probe_hook_names{};
        std::vector<std::wstring> bonus_trace_hook_names{};
        std::vector<std::wstring> stable_ability_trace_hook_names{};
        std::vector<std::wstring> context_object_reward_state_hook_names{};
        std::vector<std::wstring> kh3_load_lifecycle_hook_names{};
    };

    auto configure_runtime_hook_filters(RuntimeHookFilterConfig config) -> void;
    auto is_diagnostic_hook_name(std::wstring_view hook_name) -> bool;
    auto is_post_load_probe_hook_name(std::wstring_view hook_name) -> bool;
    auto is_bonus_trace_hook_name(std::wstring_view hook_name) -> bool;
    auto is_stable_ability_trace_hook_name(std::wstring_view hook_name) -> bool;
    auto should_log_context_object_reward_state(std::wstring_view hook_name) -> bool;
}
