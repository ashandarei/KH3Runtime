#pragma once
#include <Unreal/FWeakObjectPtr.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02EntryAssets;

    // Adapt Aqua's native base AP before her pawn/status cache is constructed.
    class Kh02AbilityCapacity
    {
      public:
        bool prepare(RC::Unreal::UObject* context, Kh02EntryAssets& assets);
        void release();

      private:
        RC::Unreal::FWeakObjectPtr base_table;
        int original_ap{};
        bool base_requested{};
    };
} // namespace kh3ap
