#pragma once
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02DarksideProjectileRepair
    {
      public:
        Kh02DarksideProjectileRepair();
        ~Kh02DarksideProjectileRepair();
        void watch(RC::Unreal::UObject* actor);
        void tick(RC::Unreal::UObject* sora, RC::Unreal::UObject* world);
        void reset() noexcept;
        auto needs_frame() const -> bool;
        auto snapshot() const -> nlohmann::json;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
