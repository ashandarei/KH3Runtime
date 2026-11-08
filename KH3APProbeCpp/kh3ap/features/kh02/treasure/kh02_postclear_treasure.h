#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace kh3ap
{
    class Kh02PostclearTreasure
    {
      public:
        void tick(RC::Unreal::UObject* pawn);
        auto snapshot() const -> nlohmann::json;

      private:
        RC::Unreal::FWeakObjectPtr world, stream;
        std::wstring package;
        bool requested{}, ready{};
    };
} // namespace kh3ap
