#include "kh02_recovery_service.h"
#include "kh02_recovery_state.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/messages/ap_message_param_storage.h"
#include <Unreal/UObject.hpp>
#include <chrono>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr float recovery_notice_display_seconds = 6.0f;
        constexpr size_t recovery_notice_max_parameter_bytes = 1024;
        constexpr auto recovery_notice_text =
            L"A serious 0.2 error occurred. Returned to GoA. Restart KH3 before entering 0.2 again.";
    } // namespace

    struct Kh02RecoveryService::Impl
    {
        explicit Impl(Kh02RecoveryConfig value) : config(std::move(value)) {}

        Kh02RecoveryConfig config;
        Kh02Story* story{};
        Kh02RecoveryState recovery;
        std::string reason;
        kh02control::Identity provider;
        RuntimeApMessageParamStorage recovery_notice_storage;

        void report(const std::string& message)
        {
            config.report(message);
        }

        void show_recovery_notice()
        {
            auto* library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            Kh02ReflectedCall notice(library, L"TresUI_OpenInformation", "kh02_recovery_notice",
                                     recovery_notice_max_parameter_bytes);
            if (!write_tres_loc_text_function_param(notice.param(L"InformationText"), notice.parameters.data(),
                                                    recovery_notice_text, recovery_notice_storage))
            {
                throw std::runtime_error("kh02_recovery_notice_text_layout");
            }
            notice.boolean(L"AutoClose", true);
            notice.number(L"OptionalDisplayTime", recovery_notice_display_seconds);
            Kh02ReflectedCall clear(library, L"TresUI_ClearInformationTextCache", "kh02_recovery_notice",
                                    recovery_notice_max_parameter_bytes);
            clear.run();
            std::vector<std::byte> retained_parameters(notice.parameters.data(),
                                                       notice.parameters.data() + notice.parameters.size());
            notice.owner->ProcessEvent(notice.function, retained_parameters.data());
            retain_runtime_ap_message_params(recovery_notice_storage, std::move(retained_parameters));
            recovery.notification_shown();
        }

        void tick_recovery(UObject* pawn)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto return_action = recovery.poll_return(now);
            if (return_action == Kh02RecoveryState::Action::timed_out)
            {
                report("kh02_recovery_return_to_garden_unavailable_restart_required");
            }
            else if (return_action == Kh02RecoveryState::Action::attempt)
            {
                try
                {
                    if (story->exit_failed_session(pawn))
                    {
                        recovery.return_dispatched(now);
                    }
                }
                catch (const std::exception& error)
                {
                    recovery.return_failed();
                    report(std::string("kh02_recovery_return_to_garden_failed:") + error.what());
                }
            }
            if (!recovery.notification_pending())
            {
                return;
            }
            if (recovery.poll_notification(now, false) == Kh02RecoveryState::Action::timed_out)
            {
                report("kh02_recovery_notice_garden_unavailable");
                return;
            }
            try
            {
                const bool garden_ready = config.can_notify() && story->recovery_garden_ready(pawn);
                const auto notice_action = recovery.poll_notification(now, garden_ready);
                if (notice_action == Kh02RecoveryState::Action::attempt)
                {
                    show_recovery_notice();
                }
            }
            catch (const std::exception& error)
            {
                report(std::string("kh02_recovery_notice_failed:") + error.what());
            }
        }
    };

    Kh02RecoveryService::Kh02RecoveryService(Kh02RecoveryConfig config)
        : impl(std::make_unique<Impl>(std::move(config)))
    {
    }

    Kh02RecoveryService::~Kh02RecoveryService() = default;

    auto Kh02RecoveryService::active() const -> bool
    {
        return impl->recovery.phase() != Kh02RecoveryState::Phase::healthy;
    }

    auto Kh02RecoveryService::needs_tick() const -> bool
    {
        return impl->recovery.return_pending() || impl->recovery.notification_pending();
    }

    void Kh02RecoveryService::begin(Kh02Story& story, const std::string& reason, kh02control::Identity provider)
    {
        if (!impl->recovery.begin(std::chrono::steady_clock::now()))
        {
            return;
        }
        impl->story = &story;
        impl->reason = reason;
        impl->provider = provider;
        story.retire_failed_session(reason);
    }

    void Kh02RecoveryService::request_return()
    {
        impl->recovery.request_return(std::chrono::steady_clock::now());
    }

    void Kh02RecoveryService::tick(UObject* pawn)
    {
        if (needs_tick())
        {
            impl->tick_recovery(pawn);
        }
    }

    auto Kh02RecoveryService::snapshot() const -> nlohmann::json
    {
        return {{"phase", impl->recovery.phase_name()},
                {"reason", impl->reason},
                {"provider_pid", impl->provider.source_pid},
                {"session_generation", impl->provider.session_generation},
                {"return_pending", impl->recovery.return_pending()},
                {"notification",
                 {{"pending", impl->recovery.notification_pending()},
                  {"attempted", impl->recovery.notification_attempted()},
                  {"shown", impl->recovery.was_notification_shown()}}}};
    }
} // namespace kh3ap
