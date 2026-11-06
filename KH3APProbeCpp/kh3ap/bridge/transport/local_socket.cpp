#include <memory>
#define NOMINMAX
#include <iterator>
#include <cstdint>
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include "kh3ap/bridge/transport/local_socket.h"
#include "kh3ap/bridge/transport/pipe_security.h"
#include <array>
#include <stdexcept>
#include <utility>

namespace kh3ap::local_ipc
{
    namespace
    {
        constexpr auto socket_listen_backlog = 4;
        constexpr auto authentication_token_bytes = 32;
        constexpr auto temporary_token_prefix_chars = 16;
        constexpr auto socket_frame_length_bytes = sizeof(uint32_t);
        constexpr auto wire_byte_bits = 8;
        constexpr auto hex_nibble_bits = 4;
        constexpr auto hex_nibble_mask = 15;
        constexpr auto winsock_version = MAKEWORD(2, 2);
        constexpr auto socket_io_wait_index = 2;
        constexpr ULONGLONG frame_io_timeout_ms = 5000;

        constexpr unsigned max_packet = 8 * 1024 * 1024;
        struct Handle
        {
            HANDLE value{};
            explicit Handle(HANDLE handle = nullptr) noexcept : value(handle)
            {
            }
            Handle(const Handle&) = delete;
            auto operator=(const Handle&) -> Handle& = delete;
            ~Handle()
            {
                if (value && value != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(value);
                }
            }
        };
        struct Socket
        {
            SOCKET value{INVALID_SOCKET};
            Socket() = default;
            Socket(const Socket&) = delete;
            auto operator=(const Socket&) -> Socket& = delete;
            ~Socket()
            {
                if (value != INVALID_SOCKET)
                {
                    closesocket(value);
                }
            }
        };
        struct Winsock
        {
            Winsock(const Winsock&) = delete;
            auto operator=(const Winsock&) -> Winsock& = delete;
            Winsock()
            {
                WSADATA data{};
                if (WSAStartup(winsock_version, &data))
                {
                    throw std::runtime_error("WSAStartup failed");
                }
            }
            ~Winsock()
            {
                WSACleanup();
            }
        };
        auto create_event() -> HANDLE
        {
            const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event)
            {
                throw std::runtime_error("local IPC event allocation failed");
            }
            return event;
        }
        auto remaining(ULONGLONG deadline) -> DWORD
        {
            if (!deadline)
            {
                return INFINITE;
            }
            const auto now = GetTickCount64();
            return now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
        }
        // Each lane owns its event and OVERLAPPED. Drain cancellation before
        // releasing a buffer, exactly as for the named-pipe implementation.
        auto transfer(SOCKET socket, char* buffer, size_t size, bool write, HANDLE event,
                      HANDLE stopped, HANDLE disconnected, ULONGLONG deadline) -> DWORD
        {
            if (WaitForSingleObject(stopped, 0) == WAIT_OBJECT_0 ||
                WaitForSingleObject(disconnected, 0) == WAIT_OBJECT_0)
            {
                throw std::runtime_error("local IPC stopped");
            }
            OVERLAPPED ov{};
            ov.hEvent = event;
            ResetEvent(event);
            WSABUF data{static_cast<ULONG>(size), buffer};
            DWORD bytes{}, flags{};
            const int result = write ? WSASend(socket, &data, 1, nullptr, 0, &ov, nullptr)
                                     : WSARecv(socket, &data, 1, nullptr, &flags, &ov, nullptr);
            if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
            {
                throw std::runtime_error("local IPC socket I/O failed");
            }
            bool ready = result == 0;
            if (!ready)
            {
                HANDLE waits[]{stopped, disconnected, event};
                ready = WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE,
                                               remaining(deadline)) ==
                        WAIT_OBJECT_0 + socket_io_wait_index;
                if (!ready)
                {
                    CancelIoEx(reinterpret_cast<HANDLE>(socket), &ov);
                }
            }
            const bool complete =
                WSAGetOverlappedResult(socket, &ov, &bytes, TRUE, &flags) != FALSE;
            if (!ready || !complete || !bytes)
            {
                throw std::runtime_error("local IPC closed or timed out");
            }
            return bytes;
        }
    } // namespace

    struct Stream::Impl
    {
        Socket socket;
        Handle read_event{create_event()}, write_event{create_event()};
        auto read(size_t size, HANDLE stopped, HANDLE disconnected, ULONGLONG& deadline)
            -> std::string
        {
            std::string result(size, '\0');
            size_t offset{};
            while (offset < size)
            {
                offset += transfer(socket.value, result.data() + offset, size - offset, false,
                                   read_event.value, stopped, disconnected, deadline);
                if (!deadline)
                {
                    deadline =
                        GetTickCount64() + frame_io_timeout_ms; // bound a partially started frame
                }
            }
            return result;
        }
    };
    Stream::Stream(std::unique_ptr<Impl> value) : impl(std::move(value))
    {
    }
    Stream::~Stream() = default;
    auto Stream::receive(void* stopped, void* disconnected, unsigned timeout) -> std::string
    {
        auto deadline = timeout == INFINITE ? ULONGLONG{} : GetTickCount64() + timeout;
        const auto header = impl->read(socket_frame_length_bytes, stopped, disconnected, deadline);
        unsigned size{};
        for (unsigned i = 0; i < socket_frame_length_bytes; ++i)
        {
            size |= unsigned(static_cast<unsigned char>(header[i])) << (wire_byte_bits * i);
        }
        if (!size || size > max_packet)
        {
            throw std::runtime_error("local IPC frame size limit");
        }
        return impl->read(size, stopped, disconnected, deadline);
    }
    auto Stream::send(std::string_view packet, void* stopped, void* disconnected) -> void
    {
        if (packet.empty() || packet.size() > max_packet)
        {
            throw std::runtime_error("local IPC frame size limit");
        }
        std::string frame;
        for (unsigned i = 0; i < socket_frame_length_bytes; ++i)
        {
            frame.push_back(static_cast<char>(packet.size() >> (wire_byte_bits * i)));
        }
        frame += packet;
        const auto deadline = GetTickCount64() + frame_io_timeout_ms;
        size_t offset{};
        while (offset < frame.size())
        {
            offset += transfer(impl->socket.value, frame.data() + offset, frame.size() - offset,
                               true, impl->write_event.value, stopped, disconnected, deadline);
        }
    }

    struct Server::Impl
    {
        Winsock winsock;
        Handle lease;
        Socket listener;
        Handle ready{create_event()};
        std::filesystem::path descriptor, temporary;
        std::string token;
        bool published{};
        ~Impl()
        {
            std::error_code ignored;
            if (published)
            {
                std::filesystem::remove(descriptor, ignored);
            }
            if (!temporary.empty())
            {
                std::filesystem::remove(temporary, ignored);
            }
        }
    };
    Server::Server(const std::filesystem::path& root) : impl(std::make_unique<Impl>())
    {
        std::filesystem::create_directories(root);
        impl->lease.value =
            CreateFileW((root / "ipc_endpoint.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
        if (impl->lease.value == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("KH3 bridge already has a local listener");
        }
        impl->listener.value =
            WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (impl->listener.value == INVALID_SOCKET)
        {
            throw std::runtime_error("local IPC socket allocation failed");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(impl->listener.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
                SOCKET_ERROR ||
            listen(impl->listener.value, socket_listen_backlog) == SOCKET_ERROR)
        {
            throw std::runtime_error("local IPC listen failed");
        }
        int length = sizeof(address);
        if (getsockname(impl->listener.value, reinterpret_cast<sockaddr*>(&address), &length) ==
                SOCKET_ERROR ||
            WSAEventSelect(impl->listener.value, impl->ready.value, FD_ACCEPT | FD_CLOSE) ==
                SOCKET_ERROR)
        {
            throw std::runtime_error("local IPC listener setup failed");
        }
        std::array<UCHAR, authentication_token_bytes> random{};
        if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        {
            throw std::runtime_error("local IPC token generation failed");
        }
        for (const auto byte : random)
        {
            impl->token += "0123456789abcdef"[byte >> hex_nibble_bits];
            impl->token += "0123456789abcdef"[byte & hex_nibble_mask];
        }
        impl->descriptor = root / "ipc_endpoint.json";
        impl->temporary =
            root / ("ipc_endpoint." + impl->token.substr(0, temporary_token_prefix_chars) + ".tmp");
        pipe::UserPipeSecurity security;
        {
            Handle output{CreateFileW(impl->temporary.c_str(), GENERIC_WRITE, 0,
                                      security.attributes(), CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                      nullptr)};
            if (output.value == INVALID_HANDLE_VALUE)
            {
                throw std::runtime_error("local IPC private descriptor creation failed");
            }
            const std::string payload =
                "{\"protocol\":\"KH3AP-v3\",\"host\":\"127.0.0.1\",\"port\":" +
                std::to_string(ntohs(address.sin_port)) + ",\"token\":\"" + impl->token +
                "\",\"pid\":" + std::to_string(GetCurrentProcessId()) + "}";
            DWORD written{};
            if (!WriteFile(output.value, payload.data(), static_cast<DWORD>(payload.size()),
                           &written, nullptr) ||
                written != payload.size() || !FlushFileBuffers(output.value))
            {
                throw std::runtime_error("local IPC descriptor write failed");
            }
        }
        if (!MoveFileExW(impl->temporary.c_str(), impl->descriptor.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            throw std::runtime_error("local IPC descriptor publish failed");
        }
        impl->published = true;
    }
    Server::~Server() = default;
    auto Server::token() const -> const std::string&
    {
        return impl->token;
    }
    auto Server::accept(void* stopped) -> std::unique_ptr<Stream>
    {
        while (true)
        {
            HANDLE waits[]{stopped, impl->ready.value};
            if (WaitForMultipleObjects(static_cast<DWORD>(std::size(waits)), waits, FALSE,
                                       INFINITE) != WAIT_OBJECT_0 + 1)
            {
                return nullptr;
            }
            WSANETWORKEVENTS events{};
            if (WSAEnumNetworkEvents(impl->listener.value, impl->ready.value, &events) ==
                SOCKET_ERROR)
            {
                throw std::runtime_error("local IPC accept event failed");
            }
            auto stream = std::make_unique<Stream::Impl>();
            stream->socket.value = ::accept(impl->listener.value, nullptr, nullptr);
            if (stream->socket.value == INVALID_SOCKET)
            {
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    continue;
                }
                throw std::runtime_error("local IPC accept failed");
            }
            const BOOL no_delay = TRUE;
            if (WSAEventSelect(stream->socket.value, nullptr, 0) == SOCKET_ERROR ||
                setsockopt(stream->socket.value, IPPROTO_TCP, TCP_NODELAY,
                           reinterpret_cast<const char*>(&no_delay),
                           sizeof(no_delay)) == SOCKET_ERROR)
            {
                throw std::runtime_error("local IPC stream setup failed");
            }
            return std::unique_ptr<Stream>{new Stream(std::move(stream))};
        }
    }
} // namespace kh3ap::local_ipc
