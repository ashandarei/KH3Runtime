#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <vector>

namespace kh3ap
{
    class Kh02WardrobeAssets;

    class Kh02WardrobeMaterials
    {
      public:
        auto backup(RC::Unreal::UObject* pawn, RC::Unreal::UObject* original, Kh02WardrobeAssets& assets)
            -> RC::Unreal::UObject*;
        void record(int index, RC::Unreal::UObject* original, RC::Unreal::UObject* adapted);
        void restore(RC::Unreal::UObject* component);

        void forget()
        {
            materials.clear();
        }

        auto empty() const -> bool
        {
            return materials.empty();
        }

        auto size() const -> std::size_t
        {
            return materials.size();
        }

      private:
        struct Material
        {
            int index;
            RC::Unreal::FWeakObjectPtr original, adapted;
        };

        std::vector<Material> materials;
    };
} // namespace kh3ap
