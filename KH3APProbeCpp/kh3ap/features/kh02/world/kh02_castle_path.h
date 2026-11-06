#pragma once
#include "kh02_castle_path_geometry.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Owns the authored path/road geometry and weak actor identities. The wired
    // source actor and its overlap delegate remain the authoritative trigger.
    class Kh02CastlePath
    {
      public:
        auto configure(RC::Unreal::UObject* context, bool expanded) -> RC::Unreal::UObject*;
        auto expanded() const -> bool;
        auto original() const -> const Kh02TriggerPlacement&;
        auto road() const -> const Kh02TriggerPlacement&;
        auto road_actor() const -> RC::Unreal::UObject*;
        auto snapshot() const -> nlohmann::json;
        void clear_world();

      private:
        RC::Unreal::FWeakObjectPtr path_trigger;
        RC::Unreal::FWeakObjectPtr road_reference;
        Kh02TriggerPlacement path_geometry;
        Kh02TriggerPlacement road_geometry;
        bool path_expanded{};
    };
} // namespace kh3ap
