#include <optional>
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bounded_read.h"
#include "kh3ap/bridge/serialization/json.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#define NOMINMAX
#include <windows.h>

namespace kh3ap
{
    namespace
    {
        constexpr auto json_escape_reserve_slack = 8;
        constexpr auto timestamp_buffer_bytes = 32;
        constexpr auto iso_year_digits = 4;
        constexpr auto iso_time_component_digits = 2;
        constexpr auto iso_microsecond_digits = 6;
        constexpr auto hex_digits_per_byte = 2;

        constexpr auto filetime_ticks_per_second = 10'000'000ULL;
        constexpr auto filetime_ticks_per_microsecond = 10ULL;
        constexpr std::wstring_view log_ellipsis = L"...";
    } // namespace

    auto to_utf8(std::wstring_view text) -> std::string
    {
        if (text.empty())
        {
            return {};
        }

        const int required_size = WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (required_size <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(required_size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                            required_size, nullptr, nullptr);
        return result;
    }

    auto json_escape(std::wstring_view text) -> std::string
    {
        std::string utf8 = to_utf8(text);
        std::string escaped;
        escaped.reserve(utf8.size() + json_escape_reserve_slack);

        for (const unsigned char ch : utf8)
        {
            switch (ch)
            {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (ch < 0x20)
                {
                    constexpr char hex_digits[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped.push_back(hex_digits[ch >> 4]);
                    escaped.push_back(hex_digits[ch & 0x0f]);
                }
                else
                {
                    escaped.push_back(static_cast<char>(ch));
                }
                break;
            }
        }

        return escaped;
    }

    auto now_iso8601_utc() -> std::string
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);

        std::tm utc{};
        gmtime_s(&utc, &time);

        char buffer[timestamp_buffer_bytes]{};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
        return std::string{buffer};
    }

    auto current_windows_filetime_100ns() noexcept -> uint64_t
    {
        FILETIME file_time{};
        GetSystemTimePreciseAsFileTime(&file_time);
        ULARGE_INTEGER value{};
        value.LowPart = file_time.dwLowDateTime;
        value.HighPart = file_time.dwHighDateTime;
        return value.QuadPart;
    }

    auto format_windows_filetime_utc(uint64_t filetime_100ns) -> std::string
    {
        if (filetime_100ns == 0)
        {
            return {};
        }

        ULARGE_INTEGER value{};
        value.QuadPart = filetime_100ns;
        FILETIME file_time{};
        file_time.dwLowDateTime = value.LowPart;
        file_time.dwHighDateTime = value.HighPart;

        SYSTEMTIME utc{};
        if (!FileTimeToSystemTime(&file_time, &utc))
        {
            return {};
        }

        const uint64_t microseconds =
            (filetime_100ns % filetime_ticks_per_second) / filetime_ticks_per_microsecond;
        std::ostringstream stream;
        stream << std::setfill('0') << std::setw(iso_year_digits) << utc.wYear << '-'
               << std::setw(iso_time_component_digits) << utc.wMonth << '-'
               << std::setw(iso_time_component_digits) << utc.wDay << 'T'
               << std::setw(iso_time_component_digits) << utc.wHour << ':'
               << std::setw(iso_time_component_digits) << utc.wMinute << ':'
               << std::setw(iso_time_component_digits) << utc.wSecond << '.'
               << std::setw(iso_microsecond_digits) << microseconds << 'Z';
        return stream.str();
    }

    auto format_bytes_hex(const void* data, size_t size) -> std::wstring
    {
        if (!data || size == 0)
        {
            return L"<none>";
        }

        std::wostringstream stream;
        stream << std::hex << std::setfill(L'0');
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t index = 0; index < size; ++index)
        {
            if (index > 0)
            {
                stream << L' ';
            }
            stream << std::setw(hex_digits_per_byte) << static_cast<unsigned int>(bytes[index]);
        }
        return stream.str();
    }

    auto read_text_file(const std::filesystem::path& path) -> std::optional<std::string>
    {
        if (json::is_seed_document(path))
        {
            try
            {
                return json::read_seed_document(path)->text;
            }
            catch (const std::exception&)
            {
                return std::nullopt;
            }
        }
        if (pipe::manages(path))
        {
            return pipe::read(path);
        }
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return std::nullopt;
        }

        return read_stream_bounded(input, json::limits::checkpoint_bytes);
    }

    auto write_text_file(const std::filesystem::path& path, std::string_view content) -> bool
    {
        if (pipe::manages(path))
        {
            return pipe::write(path, content);
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return false;
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        return output.good();
    }

    auto append_text_file(const std::filesystem::path& path, std::string_view content) -> bool
    {
        if (pipe::manages(path))
        {
            return pipe::write(path, content);
        }
        std::error_code ec{};
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        std::ofstream output(path, std::ios::binary | std::ios::app);
        if (!output)
        {
            return false;
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        return output.good();
    }

    auto truncate_for_log(std::wstring_view value, size_t max_chars) -> std::wstring
    {
        if (value.size() <= max_chars)
        {
            return std::wstring{value};
        }
        if (max_chars <= log_ellipsis.size())
        {
            return std::wstring{value.substr(0, max_chars)};
        }
        return std::wstring{value.substr(0, max_chars - log_ellipsis.size())} +
               std::wstring{log_ellipsis};
    }

    auto contains_any_wide(std::wstring_view text,
                           const std::initializer_list<std::wstring_view>& needles) -> bool
    {
        for (std::wstring_view needle : needles)
        {
            if (!needle.empty() && text.find(needle) != std::wstring_view::npos)
            {
                return true;
            }
        }
        return false;
    }
} // namespace kh3ap
