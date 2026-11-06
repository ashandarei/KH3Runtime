#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <optional>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02EntranceMirror
    {
      public:
        auto tick(RC::Unreal::UObject* scene, RC::Unreal::UObject* context, bool playing, nlohmann::json& encounter)
            -> std::optional<std::string>;
        void clear();

      private:
        bool prepare(RC::Unreal::UObject* scene, RC::Unreal::UObject* context, bool allow_retired_duplicate,
                     nlohmann::json& encounter_proof);
        RC::Unreal::FWeakObjectPtr ready_scene, pending_scene;
        std::chrono::steady_clock::time_point deadline{}, next_attempt{};
    };
} // namespace kh3ap
