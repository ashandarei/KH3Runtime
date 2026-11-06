#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <optional>

namespace kh3ap
{
    class Kh02ForestHandoff
    {
      public:
        void enqueue(RC::Unreal::UObject* world, std::wstring tag);
        auto tick(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, nlohmann::json& forest_story_proof)
            -> std::optional<std::string>;
        void clear();

      private:
        RC::Unreal::FWeakObjectPtr forest_finish_world;
        std::wstring forest_finish_tag;
    };
} // namespace kh3ap
