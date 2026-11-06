#include "kh3ap/hooks/hook_filters.h"

#include <algorithm>
#include <utility>

namespace kh3ap
{
    namespace
    {
        auto hook_filter_config() -> RuntimeHookFilterConfig&
        {
            static RuntimeHookFilterConfig config{};
            return config;
        }

        auto contains_exact_name(const std::vector<std::wstring>& names, std::wstring_view name) -> bool
        {
            return std::find(names.begin(), names.end(), name) != names.end();
        }

    }

    auto configure_runtime_hook_filters(RuntimeHookFilterConfig config) -> void
    {
        hook_filter_config() = std::move(config);
    }

    auto is_diagnostic_hook_name(std::wstring_view hook_name) -> bool
    {
        const RuntimeHookFilterConfig& config = hook_filter_config();
        return config.enable_runtime_reward_diagnostics && contains_exact_name(config.diagnostic_hook_names, hook_name);
    }

    auto is_post_load_probe_hook_name(std::wstring_view hook_name) -> bool
    {
        return contains_exact_name(hook_filter_config().post_load_probe_hook_names, hook_name);
    }

    auto is_bonus_trace_hook_name(std::wstring_view hook_name) -> bool
    {
        const RuntimeHookFilterConfig& config = hook_filter_config();
        return config.enable_bonus_trace_logging && contains_exact_name(config.bonus_trace_hook_names, hook_name);
    }

    auto is_stable_ability_trace_hook_name(std::wstring_view hook_name) -> bool
    {
        const RuntimeHookFilterConfig& config = hook_filter_config();
        return config.enable_stable_ability_trace_hooks && contains_exact_name(config.stable_ability_trace_hook_names, hook_name);
    }

    auto should_log_context_object_reward_state(std::wstring_view hook_name) -> bool
    {
        return contains_exact_name(hook_filter_config().context_object_reward_state_hook_names, hook_name);
    }

}
