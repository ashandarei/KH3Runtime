#pragma once
#include <functional>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02MovieBridge;

    // Owns presentation resources. Native story graphs retain all progression,
    // Skip and completion callbacks; this session never advances a gameflow.
    class Kh02CutsceneSession
    {
      public:
        struct Hooks
        {
            std::function<void()> prepare_localization;
            std::function<nlohmann::json(const wchar_t*, const wchar_t*, std::wstring*)> localized_message;
            std::function<const char*(RC::Unreal::UObject*)> full_movie_for_scene;
        };

        Kh02CutsceneSession(Kh02MovieBridge& movies, Hooks hooks);
        ~Kh02CutsceneSession();
        void select_full_movie(RC::Unreal::UObject* matinee, std::string logical);
        void prepare(RC::Unreal::UObject* matinee, RC::Unreal::UObject* world, RC::Unreal::UObject* pawn);
        void track_event_aqua(RC::Unreal::UObject* matinee, RC::Unreal::UObject* event_aqua);
        void bind_collapse(RC::Unreal::UObject* matinee, RC::Unreal::UObject* script);
        void tick_collapse(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, RC::Unreal::UObject* event_aqua);
        void tick(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, RC::Unreal::UObject* event_aqua);
        void cancel() noexcept;
        auto needs_frame() const -> bool;
        auto failure() const -> const std::string&;
        auto event_aqua_playing() const -> bool;
        auto collapse_actor() const -> RC::Unreal::UObject*;
        auto presentation_snapshot() const -> nlohmann::json;
        auto event_aqua_snapshot() const -> nlohmann::json;
        auto collapse_snapshot() const -> nlohmann::json;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
