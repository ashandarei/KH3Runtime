#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/grants/delivery/item_grant_service.h"

#include <UnrealDef.hpp>

#include <utility>

namespace kh3ap
{
    auto process_pending_native_traps(const RuntimeItemGrantPollDeps& deps) -> void;
    namespace
    {
        using ::kh3ap::settings::s_bridge_protocol_name;
        using ::kh3ap::settings::s_bridge_protocol_version;
        using ::kh3ap::settings::s_runtime_item_apply_event_filename;
        using ::kh3ap::settings::s_runtime_item_grant_delay;
        using ::kh3ap::settings::s_runtime_item_grant_filename;
        using ::kh3ap::settings::s_runtime_item_grant_priority_filename;
        using ::kh3ap::settings::s_runtime_item_grant_slow_wait_log_delay;
    }

    struct RuntimeItemGrantService::Impl
    {
        explicit Impl(RuntimeItemGrantServiceConfig config)
            : config_(std::move(config))
        {
        }

        auto make_apply_deps(std::wstring_view context_summary)
                -> RuntimeItemGrantApplyDeps
        {
            auto deps = config_.base_apply_deps(context_summary);
            deps.sora_current_state_cache =
                    config_.sora_current_state_cache();
            deps.progressive_keyblade_level_applied =
                    config_.progressive_keyblade_level_applied;
            return deps;
        }

        auto process_pending(
                std::wstring_view context_summary,
                std::optional<std::filesystem::path> bridge_dir,
                bool runtime_context_object_not_real) -> void
        {
            auto deps = make_runtime_item_grant_poll_deps(
                            config_.runtime_state,
                            std::move(bridge_dir),
                            s_runtime_item_grant_filename,
                            s_runtime_item_grant_priority_filename,
                            s_runtime_item_apply_event_filename,
                            s_bridge_protocol_name,
                            s_bridge_protocol_version,
                            context_summary,
                            s_runtime_item_grant_delay,
                            s_runtime_item_grant_slow_wait_log_delay,
                            runtime_context_object_not_real,
                            make_apply_deps(context_summary),
                            config_.post_apply_observer_deps(
                                    context_summary));
            process_pending_native_traps(deps);
            process_pending_runtime_item_grant(deps);
        }

        auto process_pending_now(std::wstring_view context_summary) -> void
        {
            process_pending(
                    context_summary,
                    config_.bridge_dir_resolver(),
                    config_.runtime_context_object_not_real());
        }

        RuntimeItemGrantServiceConfig config_;
    };

    RuntimeItemGrantService::RuntimeItemGrantService(
            RuntimeItemGrantServiceConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeItemGrantService::~RuntimeItemGrantService() = default;
    RuntimeItemGrantService::RuntimeItemGrantService(
            RuntimeItemGrantService&&) noexcept = default;
    auto RuntimeItemGrantService::operator=(RuntimeItemGrantService&&) noexcept
            -> RuntimeItemGrantService& = default;

    auto RuntimeItemGrantService::make_apply_deps(
            std::wstring_view context_summary) -> RuntimeItemGrantApplyDeps
    {
        return impl_->make_apply_deps(context_summary);
    }

    auto RuntimeItemGrantService::process_pending(
            std::wstring_view context_summary,
            std::optional<std::filesystem::path> bridge_dir,
            bool runtime_context_object_not_real) -> void
    {
        impl_->process_pending(
                context_summary,
                std::move(bridge_dir),
                runtime_context_object_not_real);
    }

    auto RuntimeItemGrantService::process_pending_now(
            std::wstring_view context_summary) -> void
    {
        impl_->process_pending_now(context_summary);
    }
}
