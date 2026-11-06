#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json_fwd.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Owns the source BGM slots and their cross-world retirement. Movie PCM is
    // owned by Kh02MovieBridge. No story-session pointers are retained here.
    class Kh02StoryMusic
    {
      public:
        using ResolveActorWorld = RC::Unreal::UObject* (*)(RC::Unreal::UObject*);
        explicit Kh02StoryMusic(ResolveActorWorld resolve_world);
        ~Kh02StoryMusic();
        void configure(RC::Unreal::UObject* world, int area, bool first_castle_intro);
        void prepare_battle();
        void retire_opening();
        void release_opening_event();
        void release_scene_field();
        auto release_arrival_menu(uint64_t now_ms) -> nlohmann::json;
        void play_source(RC::Unreal::UObject* caller, RC::Unreal::UObject* sound, float seek_seconds,
                         float fade_seconds, bool opening_field_handoff, const std::function<void()>& on_handoff);
        void stop_source(RC::Unreal::UObject* caller, RC::Unreal::UObject* sound, float fade_seconds);
        void set_volume(float value, float fade_seconds);
        auto is_suspended(RC::Unreal::UObject* sound) -> bool;
        auto snapshot() -> nlohmann::json;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
