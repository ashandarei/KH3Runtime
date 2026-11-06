#pragma once
#include <nlohmann/json.hpp>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    auto kh02_native_flags() -> nlohmann::json;
    auto kh02_stream_states(RC::Unreal::UObject* world, std::wstring_view prefix) -> nlohmann::json;
    auto kh02_ground_streams(RC::Unreal::UObject* world) -> nlohmann::json;
} // namespace kh3ap
