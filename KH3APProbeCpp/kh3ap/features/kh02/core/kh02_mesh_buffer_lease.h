#pragma once
#include <optional>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Explicit game-thread lease. No engine calls occur during destruction.
    // Restore retires the saved value only after native readback succeeds.
    class Kh02MeshBufferLease
    {
      public:
        void retain(RC::Unreal::UObject* context);
        void restore(RC::Unreal::UObject* context);
        auto previous() const -> std::optional<int>;
        static auto current(RC::Unreal::UObject* context) -> int;

      private:
        std::optional<int> previous_value;
    };
} // namespace kh3ap
