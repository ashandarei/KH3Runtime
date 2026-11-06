#pragma once

#include "kh3ap/core/text_format.h"

#include <filesystem>
#include <initializer_list>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    auto to_utf8(std::wstring_view text) -> std::string;
    auto json_escape(std::wstring_view text) -> std::string;
    auto now_iso8601_utc() -> std::string;
    auto current_windows_filetime_100ns() noexcept -> uint64_t;
    auto format_windows_filetime_utc(uint64_t filetime_100ns) -> std::string;
    auto format_bytes_hex(const void* data, size_t size) -> std::wstring;
    auto read_text_file(const std::filesystem::path& path) -> std::optional<std::string>;
    auto write_text_file(const std::filesystem::path& path, std::string_view content) -> bool;
    auto append_text_file(const std::filesystem::path& path, std::string_view content) -> bool;
    auto truncate_for_log(std::wstring_view value, size_t max_chars = 512) -> std::wstring;
    auto contains_any_wide(std::wstring_view text, const std::initializer_list<std::wstring_view>& needles) -> bool;
}
