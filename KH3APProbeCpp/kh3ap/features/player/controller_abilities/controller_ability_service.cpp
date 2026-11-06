#include "kh3ap/features/player/controller_abilities/host.h"
#include "kh3ap/features/player/controller_abilities/models.h"
#include <memory>
#include <utility>
namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace controller_ability_detail;
    RuntimeControllerAbilityService::Impl::Impl(RuntimeControllerAbilityConfig config)
        : config_(std::move(config)),
          setter_paths_{{
              config_.wall_run_setter_path,
              config_.style_change_setter_path,
              config_.magic_setter_path,
              config_.attraction_flow_setter_path,
              config_.airstep_setter_path,
              config_.shoot_flow_setter_path,
              STR("/Script/TresGame.TresPlayerControllerBase:BP_SetRestrictMagic"),
              STR("/Script/TresGame.TresPlayerControllerBase:BP_SetRestrictLink"),
          }}
    {
    }

    RuntimeControllerAbilityService::Impl::~Impl()
    {
        uninstall_event_hooks();
        Impl* expected = this;
        portal_instance_.compare_exchange_strong(expected, nullptr);
    }

    auto runtime_controller_unlock_ability_token(std::wstring_view runtime_token)
        -> std::wstring_view
    {
        const auto* definition = find_definition(unlock_key(runtime_token));
        return definition ? definition->enum_name : std::wstring_view{};
    }

    RuntimeControllerAbilityService::RuntimeControllerAbilityService(
        RuntimeControllerAbilityConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeControllerAbilityService::~RuntimeControllerAbilityService() = default;
    RuntimeControllerAbilityService::RuntimeControllerAbilityService(
        RuntimeControllerAbilityService&&) noexcept = default;
    auto RuntimeControllerAbilityService::operator=(RuntimeControllerAbilityService&&) noexcept
        -> RuntimeControllerAbilityService& = default;

    auto RuntimeControllerAbilityService::apply_unlock_item(
        std::wstring_view runtime_token, UObject* sora_pawn,
        RuntimeControllerAbilityGrantResult* result, std::wstring* failure_reason) -> bool
    {
        return impl_->apply_unlock_item(runtime_token, sora_pawn, result, failure_reason);
    }

    auto RuntimeControllerAbilityService::request_refresh(RuntimeControllerAbilityEvent event)
        -> void
    {
        impl_->request_refresh(event);
    }

    auto RuntimeControllerAbilityService::install_event_hooks() -> void
    {
        impl_->install_event_hooks();
    }

    auto RuntimeControllerAbilityService::uninstall_event_hooks() -> void
    {
        impl_->uninstall_event_hooks();
    }

    auto RuntimeControllerAbilityService::process_pending_refresh(
        std::chrono::steady_clock::time_point now) -> void
    {
        impl_->process_pending_refresh(now);
    }

    auto RuntimeControllerAbilityService::try_read_unlock_owned(std::wstring_view unlock_key,
                                                                bool* owned,
                                                                std::wstring* failure_reason) const
        -> bool
    {
        return impl_->try_read_unlock_owned(unlock_key, owned, failure_reason);
    }
} // namespace kh3ap
