#pragma once
#include "kh02_darkside_pillar_size.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace kh3ap
{
    // Borrowed game-thread helpers shared by the narrowly scoped diagnostic A/B.
    auto kh02_darkside_pillar_control_transform() -> RC::Unreal::UObject*;
    auto kh02_repair_darkside_pillar_origin(RC::Unreal::UObject* actor, RC::Unreal::UObject* world,
                                            const wchar_t* component_field) -> std::optional<bool>;

    class Kh02DarksidePillarRepair
    {
      public:
        ~Kh02DarksidePillarRepair();
        void watch(RC::Unreal::UObject* actor);
        void tick(RC::Unreal::UObject* world);
        void reset() noexcept;
        auto needs_frame() const -> bool;
        auto snapshot() const -> nlohmann::json;

      private:
        struct Pillar
        {
            RC::Unreal::FWeakObjectPtr actor;
            bool beam_done{}, mark_done{};
        };

        RC::Unreal::FWeakObjectPtr source_world, control_transform;
        std::vector<Pillar> pillars;
        Kh02DarksidePillarSize shaft_size;
        bool parent_prepared{}, prepare_failed{}, original_enabled{};
        std::size_t observed{}, repaired{};
        std::string error;
    };
} // namespace kh3ap
