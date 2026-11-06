#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Scene captures belong to the current test process. The command response
    // reports this path so callers never infer a developer or engine directory.
    inline auto kh02_capture_file(std::wstring_view basename) -> std::filesystem::path
    {
        const std::filesystem::path filename{basename};
        if (filename.empty() || filename.has_parent_path() || filename.extension() != L".png")
        {
            throw std::invalid_argument("kh02_capture_filename");
        }
        const auto directory =
            std::filesystem::temp_directory_path() / "KH3AP" / "captures" / std::to_wstring(GetCurrentProcessId());
        std::filesystem::create_directories(directory);
        return directory / filename;
    }

    inline auto kh02_capture_command(const std::filesystem::path& file) -> std::wstring
    {
        return L"HighResShot 1920x1080 filename=\"" + file.generic_wstring() + L"\"";
    }
} // namespace kh3ap
