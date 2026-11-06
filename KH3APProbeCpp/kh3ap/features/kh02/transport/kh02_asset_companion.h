#pragma once
#include "shared/kh02/kh02_control_pipe.h"
#include <string>
#include <string_view>

namespace kh3ap
{
    struct Kh02CompanionSession
    {
        kh02control::Identity identity;
        bool running{};
        bool owned{};
        bool executable_verified{};
        bool input_desktop_known{};
        bool input_desktop_matches{};
        bool ready{};
        bool input_isolated{};
    };

    // Runtime callers use typed session state. JSON wrappers below are diagnostics only.
    auto start_kh02_companion_session(std::wstring_view executable) -> Kh02CompanionSession;
    auto kh02_companion_session() -> Kh02CompanionSession;
    // Cheap health snapshot; does not enumerate windows or query the input desktop.
    auto kh02_companion_health() -> Kh02CompanionSession;
    auto stop_kh02_companion_session(const kh02control::Identity& expected) -> bool;
    auto kh02_provider_enabled() -> bool;
    auto prepare_kh02_provider() -> bool;
    auto kh02_provider_completion_ready() -> bool;
    void mark_kh02_provider_content_requested();
    void reset_kh02_provider_preparation();
    void set_kh02_companion_ready(const kh02control::Identity& identity);
    auto submit_kh02_movie_request(kh02control::Request request) -> std::shared_ptr<kh02control::Ticket>;
    // The calling KH3 process owns the job handle; closing/crashing KH3 kills its
    // companion. Windows uses an inactive private desktop; Wine hides the owned
    // child's windows through events and verifies isolation before movie requests.
    auto start_kh02_asset_companion(std::wstring_view executable) -> std::string;
    auto kh02_asset_companion_status() -> std::string;
    auto stop_kh02_asset_companion() -> std::string;
} // namespace kh3ap
