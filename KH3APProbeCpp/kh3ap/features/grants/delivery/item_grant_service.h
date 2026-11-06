#pragma once

#include "kh3ap/features/grants/delivery/item_grant_apply_helpers.h"
#include "kh3ap/features/grants/delivery/item_grant_poll_helpers.h"
#include "kh3ap/features/player/state/sora_current_state_cache_view.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;

    struct RuntimeItemGrantServiceConfig
    {
        RuntimeState& runtime_state;
        std::function<RuntimeSoraCurrentStateCacheView()>
                sora_current_state_cache{};
        std::function<std::optional<std::filesystem::path>()>
                bridge_dir_resolver{};
        std::function<bool()> runtime_context_object_not_real{};
        std::function<RuntimeItemGrantApplyDeps(std::wstring_view)>
                base_apply_deps{};
        std::function<RuntimeItemGrantPostApplyObserverDeps(std::wstring_view)>
                post_apply_observer_deps{};
        std::function<void()> progressive_keyblade_level_applied{};
    };

    // Owns production runtime-item grant dependency composition. In
    // particular, every caller receives the same cached-Sora state view.
    class RuntimeItemGrantService
    {
    public:
        explicit RuntimeItemGrantService(RuntimeItemGrantServiceConfig config);
        ~RuntimeItemGrantService();

        RuntimeItemGrantService(const RuntimeItemGrantService&) = delete;
        auto operator=(const RuntimeItemGrantService&)
                -> RuntimeItemGrantService& = delete;
        RuntimeItemGrantService(RuntimeItemGrantService&&) noexcept;
        auto operator=(RuntimeItemGrantService&&) noexcept
                -> RuntimeItemGrantService&;

        auto make_apply_deps(std::wstring_view context_summary)
                -> RuntimeItemGrantApplyDeps;
        auto process_pending(
                std::wstring_view context_summary,
                std::optional<std::filesystem::path> bridge_dir,
                bool runtime_context_object_not_real) -> void;
        auto process_pending_now(std::wstring_view context_summary) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
