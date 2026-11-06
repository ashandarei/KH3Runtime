#include "kh02_asset_companion.h"
#include "kh02_companion_process.h"
#include "kh02_provider_preparation.h"

namespace kh3ap
{
    namespace
    {
        auto companion_process() -> Kh02CompanionProcess&
        {
            static Kh02CompanionProcess process;
            return process;
        }

        auto provider_preparation() -> Kh02ProviderPreparation&
        {
            // Register process destruction first, so preparation cancellation and
            // worker completion always precede job/process/desktop destruction.
            auto& process = companion_process();
            static Kh02ProviderPreparation preparation(process);
            return preparation;
        }
    } // namespace

    auto kh02_asset_companion_status() -> std::string
    {
        return companion_process().status();
    }

    auto stop_kh02_asset_companion() -> std::string
    {
        return companion_process().stop();
    }

    auto start_kh02_companion_session(std::wstring_view executable) -> Kh02CompanionSession
    {
        return companion_process().start(executable);
    }

    auto kh02_companion_session() -> Kh02CompanionSession
    {
        return companion_process().session();
    }

    auto kh02_companion_health() -> Kh02CompanionSession
    {
        return companion_process().health();
    }

    auto stop_kh02_companion_session(const kh02control::Identity& expected) -> bool
    {
        return companion_process().stop(expected);
    }

    auto kh02_provider_enabled() -> bool
    {
        return Kh02ProviderPreparation::enabled();
    }

    auto prepare_kh02_provider() -> bool
    {
        return provider_preparation().prepare();
    }

    auto kh02_provider_completion_ready() -> bool
    {
        return provider_preparation().completion_ready();
    }

    void reset_kh02_provider_preparation()
    {
        provider_preparation().reset();
    }

    void mark_kh02_provider_content_requested()
    {
        provider_preparation().mark_content_requested();
    }

    void set_kh02_companion_ready(const kh02control::Identity& identity)
    {
        companion_process().set_ready(identity);
    }

    auto submit_kh02_movie_request(kh02control::Request request) -> std::shared_ptr<kh02control::Ticket>
    {
        return companion_process().submit(request);
    }

    auto start_kh02_asset_companion(std::wstring_view executable) -> std::string
    {
        start_kh02_companion_session(executable);
        return kh02_asset_companion_status();
    }
} // namespace kh3ap
