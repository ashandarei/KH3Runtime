// Local equivalence verification uses pipes, keeping game payloads off disk.
#include "kh3ap/features/kh02/transport/kh02_package_format.h"
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <iostream>

int main()
{
    using namespace kh02asset;
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    const auto read = [](void* output, std::size_t size)
    {
        return static_cast<bool>(std::cin.read(static_cast<char*>(output), size));
    };
    for (;;)
    {
        uint32_t lengths[3]{};
        if (!read(lengths, sizeof(lengths)))
        {
            return std::cin.eof() ? 0 : 1;
        }
        if (!lengths[0] || lengths[0] > 1024 || lengths[1] > package::maximum_header_bytes ||
            lengths[2] > package::maximum_export_bytes)
        {
            return 1;
        }
        std::string name(lengths[0], '\0');
        Bytes header(lengths[1]), exports(lengths[2]);
        if (!read(name.data(), name.size()) || !read(header.data(), header.size()) ||
            !read(exports.data(), exports.size()))
        {
            return 1;
        }
        try
        {
            const auto result = adapt_package(name, header, exports);
            const uint32_t success{};
            std::cout.write(reinterpret_cast<const char*>(&success), sizeof(success));
            std::cout.write(reinterpret_cast<const char*>(result.header.data()),
                            result.header.size());
            std::cout.write(reinterpret_cast<const char*>(result.exports.data()),
                            result.exports.size());
        }
        catch (const std::exception& error)
        {
            const std::string message = error.what();
            const auto length = static_cast<uint32_t>(message.size());
            std::cout.write(reinterpret_cast<const char*>(&length), sizeof(length));
            std::cout.write(message.data(), message.size());
        }
        std::cout.flush();
    }
}
