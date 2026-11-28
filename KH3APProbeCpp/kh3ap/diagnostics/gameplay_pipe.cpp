#include "kh3ap/diagnostics/gameplay_pipe.h"
#include "kh3ap/bridge/transport/pipe_security.h"

#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace kh3ap
{
    namespace
    {
        constexpr DWORD request_bytes = 8192;
        constexpr DWORD reply_bytes = 2 * 1024 * 1024;
        constexpr DWORD reply_wait_ms = DiagnosticGameplayPipe::max_request_lifetime_ms + 500;
        constexpr DWORD write_timeout_ms = 2000;

        struct Handle
        {
            HANDLE value;
            ~Handle()
            {
                if (value && value != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(value);
                }
            }
        };
    }

    struct DiagnosticGameplayPipe::Impl
    {
        Handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        Handle reply_ready{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
        std::mutex mutex;
        std::atomic<bool> pending{};
        std::optional<Request> request;
        std::string reply;
        uint64_t ticket{};
        uint64_t active_ticket{};
        std::thread worker;

        Impl()
        {
            if (!stop.value || !reply_ready.value)
            {
                return;
            }
            try
            {
                worker = std::thread([this] {
                    try
                    {
                        listen();
                    }
                    catch (const std::exception&)
                    {
                        // Diagnostics can be unavailable without affecting gameplay.
                    }
                });
            }
            catch (const std::system_error&)
            {
            }
        }

        ~Impl()
        {
            if (worker.joinable())
            {
                SetEvent(stop.value);
                worker.join();
            }
        }

        auto finish_io(HANDLE pipe, OVERLAPPED& io, BOOL success, DWORD error,
                       DWORD& bytes, DWORD timeout) const -> bool
        {
            if (success)
            {
                return true;
            }
            if (error != ERROR_IO_PENDING)
            {
                return false;
            }
            const HANDLE waits[]{stop.value, io.hEvent};
            if (WaitForMultipleObjects(2, waits, FALSE, timeout) == WAIT_OBJECT_0 + 1)
            {
                return GetOverlappedResult(pipe, &io, &bytes, FALSE);
            }
            CancelIoEx(pipe, &io);
            GetOverlappedResult(pipe, &io, &bytes, TRUE);
            return false;
        }

        auto dispatch(std::string text) -> std::string
        {
            {
                std::lock_guard lock(mutex);
                ResetEvent(reply_ready.value);
                active_ticket = ++ticket;
                request.emplace(Request{ticket, std::move(text)});
                pending.store(true, std::memory_order_release);
            }
            const HANDLE waits[]{stop.value, reply_ready.value};
            const auto result = WaitForMultipleObjects(2, waits, FALSE, reply_wait_ms);
            std::lock_guard lock(mutex);
            active_ticket = 0;
            request.reset();
            pending.store(false, std::memory_order_release);
            if (result != WAIT_OBJECT_0 + 1)
            {
                reply.clear();
                return {};
            }
            return std::exchange(reply, {});
        }

        void serve(HANDLE pipe, HANDLE event)
        {
            char buffer[request_bytes];
            while (WaitForSingleObject(stop.value, 0) != WAIT_OBJECT_0)
            {
                OVERLAPPED io{};
                io.hEvent = event;
                ResetEvent(event);
                DWORD bytes{};
                const BOOL read = ReadFile(pipe, buffer, request_bytes, &bytes, &io);
                if (!finish_io(pipe, io, read, read ? ERROR_SUCCESS : GetLastError(), bytes, INFINITE) ||
                    !bytes)
                {
                    return;
                }
                auto response = dispatch(std::string(buffer, bytes));
                if (response.empty() || response.size() > reply_bytes)
                {
                    return;
                }
                io = {};
                io.hEvent = event;
                ResetEvent(event);
                const auto size = static_cast<DWORD>(response.size());
                const BOOL written = WriteFile(pipe, response.data(), size, &bytes, &io);
                if (!finish_io(pipe, io, written, written ? ERROR_SUCCESS : GetLastError(),
                               bytes, write_timeout_ms) || bytes != size)
                {
                    return;
                }
            }
        }

        void listen()
        {
            pipe::UserPipeSecurity security;
            const auto name = L"\\\\.\\pipe\\KH3AP-diagnostics-v1-" + std::to_wstring(GetCurrentProcessId());
            Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
            Handle pipe{CreateNamedPipeW(name.c_str(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                1, 65536, request_bytes, 0, security.attributes())};
            if (!event.value || pipe.value == INVALID_HANDLE_VALUE)
            {
                return;
            }
            while (WaitForSingleObject(stop.value, 0) != WAIT_OBJECT_0)
            {
                OVERLAPPED io{};
                io.hEvent = event.value;
                ResetEvent(event.value);
                DWORD bytes{};
                const BOOL connected = ConnectNamedPipe(pipe.value, &io);
                const auto error = connected ? ERROR_SUCCESS : GetLastError();
                if (error != ERROR_PIPE_CONNECTED &&
                    !finish_io(pipe.value, io, connected, error, bytes, INFINITE))
                {
                    return;
                }
                serve(pipe.value, event.value);
                DisconnectNamedPipe(pipe.value);
            }
        }
    };

    DiagnosticGameplayPipe::DiagnosticGameplayPipe() : impl_(std::make_unique<Impl>())
    {
    }

    DiagnosticGameplayPipe::~DiagnosticGameplayPipe() = default;

    auto DiagnosticGameplayPipe::take_request() -> std::optional<Request>
    {
        if (!impl_->pending.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }
        std::lock_guard lock(impl_->mutex);
        impl_->pending.store(false, std::memory_order_release);
        return std::exchange(impl_->request, std::nullopt);
    }

    void DiagnosticGameplayPipe::complete(uint64_t ticket, std::string reply)
    {
        std::lock_guard lock(impl_->mutex);
        if (ticket == impl_->active_ticket)
        {
            impl_->reply = std::move(reply);
            SetEvent(impl_->reply_ready.value);
        }
    }
}
