#include "kh3ap/bridge/transport/checkpoint_file.h"
#include "kh3ap/bridge/transport/bounded_read.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <fstream>
#include <string>
#include <system_error>

namespace kh3ap
{
    namespace
    {
        auto matches(const std::filesystem::path& path, std::string_view expected) -> bool
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return false;
            }
            const auto actual = read_stream_bounded(input, expected.size());
            return actual && *actual == expected;
        }
    } // namespace

    auto write_checkpoint_file_atomic_verified(const std::filesystem::path& path,
                                               std::string_view content) -> bool
    {
        std::error_code error;
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
            {
                return false;
            }
        }
        auto temporary = path;
        temporary += L".tmp";
        const auto remove_temporary = [&]()
        {
            std::filesystem::remove(temporary, error);
        };
        remove_temporary();
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                return false;
            }
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();
            if (!output)
            {
                remove_temporary();
                return false;
            }
        }
        if (!matches(temporary, content) ||
            !MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            remove_temporary();
            return false;
        }
        return matches(path, content);
    }
} // namespace kh3ap
