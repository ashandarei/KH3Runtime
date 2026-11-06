#pragma once
#include "shared/kh02/kh02_control_pipe.h"
#include "shared/kh02/kh02_movie_shared.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace kh3ap
{
    // A decoder lease has no UI or audio ownership. The bridge explicitly stops
    // its audio between cancelling the ticket and releasing the shared mapping.
    class Kh02MovieDecoderLease
    {
      public:
        Kh02MovieDecoderLease() = default;
        ~Kh02MovieDecoderLease();
        Kh02MovieDecoderLease(const Kh02MovieDecoderLease&) = delete;
        auto operator=(const Kh02MovieDecoderLease&) -> Kh02MovieDecoderLease& = delete;
        void open_provider(uint32_t pid);
        void create_mapping();
        void request_stream(const kh02control::Identity& identity, std::string_view file, bool audible);
        void check_provider() const;
        void check_reply() const;
        void cancel_request() noexcept;
        void release_mapping() noexcept;
        void close_provider() noexcept;

        auto frames() const -> kh02movie::SharedFrames*
        {
            return shared.get();
        }

        auto provider_pid() const -> uint32_t
        {
            return source_pid;
        }

        auto request_id() const -> const std::string&
        {
            return control_request_id;
        }

        auto request_pending() const -> bool;

      private:
        std::unique_ptr<kh02movie::SharedFrames> shared;
        void* source_process{};
        std::shared_ptr<kh02control::Ticket> control_request;
        uint32_t source_pid{};
        std::string control_request_id;
    };
} // namespace kh3ap
