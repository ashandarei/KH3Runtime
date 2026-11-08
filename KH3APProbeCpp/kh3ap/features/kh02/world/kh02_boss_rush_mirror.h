#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>

namespace kh3ap
{
    // BeginPlay supplies the exact authored mirror. No actor discovery is
    // needed while waiting for the chapter's completion flag.
    class Kh02BossRushMirror
    {
      public:
        void watch(RC::Unreal::UObject* actor);
        void tick(RC::Unreal::UObject* world, bool chapter_complete);
        void clear_world();
        auto snapshot() const -> nlohmann::json;

      private:
        RC::Unreal::FWeakObjectPtr mirror;
        bool start_requested{};
    };
} // namespace kh3ap
