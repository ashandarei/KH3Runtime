#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Reconciles source vine visibility after BeginPlay/overlap notifications
    // and hero replacement. No scan or update is needed while nothing is dirty.
    class Kh02ForestVisibility
    {
      public:
        auto watch(RC::Unreal::UObject* actor) -> std::optional<std::string>;
        auto tick(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, bool ready) -> std::optional<std::string>;
        auto events() const -> const nlohmann::json&;
        void clear_world();

      private:
        struct Volume
        {
            RC::Unreal::FWeakObjectPtr actor;
            bool dirty{true};
        };

        std::vector<Volume> forest_lod_volumes;
        RC::Unreal::FWeakObjectPtr forest_lod_player;
        bool forest_lod_pending{};
        nlohmann::json reconciliation_events = nlohmann::json::array();
    };
} // namespace kh3ap
