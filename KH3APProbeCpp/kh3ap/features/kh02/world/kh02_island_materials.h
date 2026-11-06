#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <cstddef>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Rebuild imported cloud render state through component-owned MIDs. This
    // owner retains only weak world identity and the number of adapted slots.
    class Kh02IslandMaterials
    {
      public:
        void prepare(RC::Unreal::UObject* context, RC::Unreal::UObject* world);
        auto adapted_slots() const -> std::size_t;
        void clear_world();

      private:
        RC::Unreal::FWeakObjectPtr material_world;
        std::size_t material_slots{};
        bool island_world{};
    };
} // namespace kh3ap
