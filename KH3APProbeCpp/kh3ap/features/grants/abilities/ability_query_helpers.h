#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UFunction;
    class UObject;
}

namespace kh3ap
{
    auto write_ability_kind_query_param(
            RC::Unreal::UFunction* function_object,
            void* params,
            uint64_t ability_value) -> bool;

    auto format_function_return_value(
            RC::Unreal::UFunction* function_object,
            void* params) -> std::wstring;

}
