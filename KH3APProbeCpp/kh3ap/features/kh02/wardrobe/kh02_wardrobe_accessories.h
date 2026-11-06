#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <string>
#include <vector>

namespace kh3ap
{
    class Kh02WardrobeAccessories
    {
      public:
        void attach(RC::Unreal::UObject* pawn, RC::Unreal::UObject* component, RC::Unreal::UObject* prototype,
                    const std::wstring& socket);
        void clear();

        void forget()
        {
            accessories.clear();
        }

        auto actors() const -> const std::vector<RC::Unreal::FWeakObjectPtr>&
        {
            return accessories;
        }

      private:
        std::vector<RC::Unreal::FWeakObjectPtr> accessories;
    };
} // namespace kh3ap
