#include "kh3ap/bridge/rewards/reconciliation_protocol.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#define NOMINMAX
#include <windows.h>

namespace kh3ap::reconciliation::protocol
{
    namespace
    {
        constexpr auto max_exact_json_integer = 9007199254740991.0;
    }
    auto utf8_to_wstring(std::string_view value) -> std::wstring
    {
        if (value.empty())
        {
            return {};
        }
        const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                              static_cast<int>(value.size()), nullptr, 0);
        if (!size)
        {
            throw std::runtime_error("invalid_utf8");
        }
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size);
        return result;
    }
    auto wstring_to_utf8(std::wstring_view value) -> std::string
    {
        if (value.empty())
        {
            return {};
        }
        const auto size =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (!size)
        {
            throw std::runtime_error("invalid_utf16");
        }
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }
    auto field(const Json& j, std::string_view k) -> const Json&
    {
        static const Json nil;
        return j.contains(k) ? j[k] : nil;
    }
    auto text(const Json& j) -> std::string
    {
        return j.is_string() ? j.get_ref<const std::string&>() : "";
    }
    auto number(const Json& j, int64_t fallback) -> int64_t
    {
        if (!j.is_number())
        {
            return fallback;
        }
        const auto n = j.get<double>();
        if (!std::isfinite(n) || std::floor(n) != n || std::abs(n) > max_exact_json_integer)
        {
            throw std::runtime_error("invalid_integer");
        }
        return static_cast<int64_t>(n);
    }
    auto flag(const Json& j) -> bool
    {
        return j.is_boolean() && j.get<bool>();
    }
    auto encode(const Json& j) -> std::string
    {
        std::string out;
        kh3ap::json::write(j, out);
        return out;
    }
    auto decode(std::string_view s, size_t max_bytes) -> Json
    {
        if (s.size() > max_bytes)
        {
            throw std::runtime_error("json_size_limit");
        }
        Json j;
        if (!kh3ap::json::parse(j, s))
        {
            throw std::runtime_error("invalid_json");
        }
        return j;
    }
    auto array(const Json& j) -> const Json::array_t&
    {
        if (!j.is_array())
        {
            throw std::runtime_error("invalid_array");
        }
        return j.get_ref<const Json::array_t&>();
    }
    auto receipt_json(const Receipts& r) -> Json
    {
        Json::array_t extra;
        for (auto index : r.extra)
        {
            extra.emplace_back(index);
        }
        return Json{{"version", 1}, {"through", r.through}, {"extra", std::move(extra)}};
    }
    auto receipts(const Json& j) -> Receipts
    {
        if (number(field(j, "version")) != 1 || number(field(j, "through")) < -1)
        {
            throw std::runtime_error("invalid_receipt_snapshot");
        }
        Receipts r{number(field(j, "through"))};
        for (const auto& value : array(field(j, "extra")))
        {
            const auto index = number(value);
            if (index < 0)
            {
                throw std::runtime_error("invalid_receipt_index");
            }
            r.add(index);
        }
        return r;
    }
    auto utc(uint64_t ticks) -> std::string
    {
        FILETIME ft{};
        if (!ticks)
        {
            GetSystemTimeAsFileTime(&ft);
        }
        else
        {
            ft.dwLowDateTime = static_cast<DWORD>(ticks);
            ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
        }
        SYSTEMTIME st{};
        FileTimeToSystemTime(&ft, &st);
        char out[40];
        snprintf(out, sizeof(out), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", st.wYear, st.wMonth,
                 st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return out;
    }
    auto slot_path(int64_t slot) -> std::string
    {
        return "kh3sv2/data/KHIII_slot" + std::to_string(slot) + ".bin";
    }
    auto map_path(const Json& context) -> std::string
    {
        for (auto name : {"world", "outer", "object", "pawn"})
        {
            const auto value = text(field(context, name));
            auto begin = value.find("/Game/Levels/");
            if (begin == std::string::npos)
            {
                begin = value.find("/Game/Maps/");
            }
            if (begin == std::string::npos)
            {
                continue;
            }
            auto end = value.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_/-", begin);
            return value.substr(begin, end - begin);
        }
        return {};
    }
} // namespace kh3ap::reconciliation::protocol
