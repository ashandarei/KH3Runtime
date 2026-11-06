#pragma once
#include "shared/kh02/kh02_control_pipe.h"
#include <atomic>
#include <memory>
#include <thread>
#include <exception>

namespace kh3ap
{
    class Kh02CompanionProcess;

    // The process must outlive this owner: cancellation joins its readiness worker first.
    class Kh02ProviderPreparation
    {
      public:
        explicit Kh02ProviderPreparation(Kh02CompanionProcess& process);
        ~Kh02ProviderPreparation();
        static auto enabled() -> bool;
        auto prepare() -> bool;
        auto completion_ready() const -> bool;
        void reset();
        void mark_content_requested();

      private:
        Kh02CompanionProcess& process;
        std::jthread pending;
        std::atomic<bool> completed{};
        kh02control::Identity prepared_identity;
        std::exception_ptr preparation_error;
        std::shared_ptr<std::atomic<bool>> stopping = std::make_shared<std::atomic<bool>>(false);
        kh02control::Identity bound;
        kh02control::Identity imported_identity;
        bool binding_failed{};
    };
} // namespace kh3ap
