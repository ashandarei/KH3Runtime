#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace kh3ap
{
    // A single in-flight command. The worker owns I/O and the game thread
    // keeps exclusive ownership of gameplay, including timeout validation.
    class DiagnosticGameplayPipe
    {
    public:
        struct Request
        {
            uint64_t ticket;
            std::string text;
        };

        static constexpr int max_request_lifetime_ms = 8000;

        DiagnosticGameplayPipe();
        ~DiagnosticGameplayPipe();
        auto take_request() -> std::optional<Request>;
        void complete(uint64_t ticket, std::string reply);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
