#pragma once
#include "kh3ap/core/objects/uobject_weak.h"
#include <span>
#include <cstdint>

namespace kh3ap
{
    class Kh02MovieSurface
    {
        RC::Unreal::FWeakObjectPtr asset_, player_, texture_, target_, context_;

      public:
        // Native subtitles use 190 and the cutscene pause menu uses 200.
        static constexpr uint8_t priority = 189;
        ~Kh02MovieSurface();
        void prepare(RC::Unreal::UObject* context);
        void show();
        void upload(std::span<const uint8_t> pixels);
        void stop() noexcept;
        auto texture() const -> RC::Unreal::UObject*;
    };
} // namespace kh3ap
