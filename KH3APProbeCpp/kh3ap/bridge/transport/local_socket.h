#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace kh3ap::local_ipc
{
    inline constexpr unsigned infinite_receive_timeout = 0xffffffff;

    // Framing only. The caller owns v3 state, ACKs, replay and both I/O lanes.
    class Stream
    {
        struct Impl;
        std::unique_ptr<Impl> impl;
        explicit Stream(std::unique_ptr<Impl> value);
        friend class Server;

    public:
        ~Stream();
        auto receive(void* stopped, void* disconnected, unsigned timeout = infinite_receive_timeout)
            -> std::string;
        auto send(std::string_view packet, void* stopped, void* disconnected) -> void;
    };
    class Server
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

    public:
        explicit Server(const std::filesystem::path& root);
        ~Server();
        auto accept(void* stopped) -> std::unique_ptr<Stream>;
        auto token() const -> const std::string&;
    };
} // namespace kh3ap::local_ipc
