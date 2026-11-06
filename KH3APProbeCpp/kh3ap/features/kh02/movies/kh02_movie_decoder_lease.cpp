#include "kh02_movie_decoder_lease.h"
#include "kh3ap/features/kh02/transport/kh02_asset_companion.h"
#define NOMINMAX
#include <Windows.h>
#include <stdexcept>

namespace kh3ap
{
    Kh02MovieDecoderLease::~Kh02MovieDecoderLease()
    {
        cancel_request();
        release_mapping();
        close_provider();
    }

    void Kh02MovieDecoderLease::open_provider(uint32_t pid)
    {
        source_pid = pid;
        source_process = OpenProcess(SYNCHRONIZE, FALSE, source_pid);
        if (!source_process)
        {
            throw std::runtime_error("movie_bridge_provider_process");
        }
    }

    void Kh02MovieDecoderLease::create_mapping()
    {
        shared = std::make_unique<kh02movie::SharedFrames>();
        shared->create(source_pid);
    }

    void Kh02MovieDecoderLease::request_stream(const kh02control::Identity& identity, std::string_view file,
                                               bool audible)
    {
        kh02control::Request request;
        request.identity = identity;
        request.operation = kh02control::Operation::stream;
        request.flags = kh02control::initialize_ui | (audible ? kh02control::audible : 0);
        if (!kh02control::assign(request.movie, file) || !kh02control::assign(request.shared_name, shared->name()))
        {
            throw std::runtime_error("movie_bridge_control_request_bounds");
        }
        control_request = submit_kh02_movie_request(request);
        control_request_id = std::to_string(control_request->generation);
    }

    void Kh02MovieDecoderLease::check_provider() const
    {
        if (!shared || !source_process || WaitForSingleObject(source_process, 0) != WAIT_TIMEOUT)
        {
            throw std::runtime_error("movie_bridge_provider_exited");
        }
    }

    void Kh02MovieDecoderLease::check_reply() const
    {
        if (control_request && control_request->complete.load(std::memory_order_acquire) &&
            control_request->reply.status != kh02control::Status::accepted)
        {
            throw std::runtime_error("movie_bridge_control_rejected:" +
                                     std::to_string(static_cast<uint32_t>(control_request->reply.status)) + ":" +
                                     std::string(kh02control::text(control_request->reply.error)));
        }
    }

    void Kh02MovieDecoderLease::cancel_request() noexcept
    {
        if (control_request)
        {
            control_request->canceled.store(true, std::memory_order_release);
            control_request.reset();
        }
    }

    void Kh02MovieDecoderLease::release_mapping() noexcept
    {
        if (shared)
        {
            try
            {
                shared->signal(kh02movie::Command::cancel);
            }
            catch (...)
            {
            }
            shared.reset();
        }
    }

    void Kh02MovieDecoderLease::close_provider() noexcept
    {
        if (source_process)
        {
            CloseHandle(source_process);
            source_process = nullptr;
        }
    }

    auto Kh02MovieDecoderLease::request_pending() const -> bool
    {
        return control_request && !control_request->complete.load(std::memory_order_acquire);
    }
} // namespace kh3ap
