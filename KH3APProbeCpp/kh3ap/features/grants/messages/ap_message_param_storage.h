#pragma once

#include <Unreal/FString.hpp>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace kh3ap
{
    struct RetainedFStringArray
    {
        uint64_t data_ptr{};
        int32_t num{};
        int32_t max{};
    };

    class RuntimeApMessageParamStorage
    {
    public:
        static constexpr size_t max_entries = 512;

        auto retain_bytes(std::vector<std::byte> params, size_t max_entries) -> void;
        auto retain_string_array(std::wstring_view value, size_t max_entries) -> RetainedFStringArray;

    private:
        std::mutex byte_mutex_{};
        std::vector<std::vector<std::byte>> byte_storage_{};
        std::mutex string_array_mutex_{};
        std::vector<std::vector<RC::Unreal::FString>> string_array_storage_{};
    };
}
