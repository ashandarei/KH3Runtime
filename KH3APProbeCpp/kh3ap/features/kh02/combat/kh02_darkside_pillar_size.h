#pragma once
#include "kh02_darkside_pillar_policy.h"
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    // Owns only the imported shaft's constant-size compatibility edit.
    class Kh02DarksidePillarSize
    {
      public:
        ~Kh02DarksidePillarSize();
        void prepare(RC::Unreal::UObject* source_size_module, RC::Unreal::UObject* native_mesh);
        void reset() noexcept;
        auto ready() const -> bool;

      private:
        RC::Unreal::FWeakObjectPtr module, distribution, host_mesh;
        Kh02PillarSizeValues original{}, applied{};
        bool prepared{};
    };
} // namespace kh3ap
