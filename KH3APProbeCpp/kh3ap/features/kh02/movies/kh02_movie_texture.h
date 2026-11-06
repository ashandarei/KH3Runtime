#pragma once
#include <cstdint>
#include <span>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    auto create_kh02_movie_texture(RC::Unreal::UObject* owner) -> RC::Unreal::UObject*;
    void upload_kh02_movie_texture(RC::Unreal::UObject* texture, std::span<const uint8_t> pixels);
} // namespace kh3ap
