#pragma once

#include <cstdint>
#include <string>

namespace kh3ap
{
    struct PropertyProbeEntry
    {
        std::wstring kind{};
        std::wstring reason{};
        std::wstring object_name{};
        std::wstring class_name{};
        std::wstring property_name{};
        std::wstring property_class{};
        int offset{-1};
        int element_size{-1};
        std::wstring value{};
    };

    struct RuntimeBlockingUiProbeUpdateData
    {
        std::wstring reason{};
        std::wstring context_name{};
        std::wstring event_name{};
        int64_t callback_id{-1};
        bool next_active{false};
    };

}
