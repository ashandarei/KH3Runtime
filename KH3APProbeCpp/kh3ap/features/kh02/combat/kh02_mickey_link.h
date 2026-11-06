#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02MickeyLink
    {
      public:
        Kh02MickeyLink();
        ~Kh02MickeyLink();
        void tick(const std::function<RC::Unreal::UObject*()>& player,
                  const std::function<bool(RC::Unreal::UObject*)>& change_to_aqua, bool companion_pending);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto snapshot() const -> std::string;
        auto offer(RC::Unreal::UObject* pawn) -> bool;
#endif
      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
