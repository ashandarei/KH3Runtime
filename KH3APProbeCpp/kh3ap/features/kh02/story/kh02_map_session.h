#pragma once
#include "kh02_map_session_state.h"
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
    // Coordinates arrival phases and source authority with separate presentation and registry owners.
    // Hooks read source readiness; only the original source graph advances story.
    class Kh02MapSession
    {
      public:
        using Generation = Kh02MapSessionState::Generation;

        struct Hooks
        {
            std::function<std::string(RC::Unreal::UObject*)> entry_error;
            std::function<nlohmann::json(RC::Unreal::UObject*)> opening_frame;
            std::function<nlohmann::json(std::uint64_t)> release_menu_music;
            std::function<bool(RC::Unreal::UObject*)> finish_loading;
            std::function<bool(RC::Unreal::UObject*)> opening_required;
            std::function<bool(RC::Unreal::UObject*)> source_initialized;
            std::function<nlohmann::json(RC::Unreal::UObject*, RC::Unreal::UObject*)> source_readiness;
            std::function<void()> prepare_music;
            std::function<void()> retire_opening_music;
            std::function<void()> retire_presentation;
            std::function<bool(RC::Unreal::UObject*)> source_scene_playing;
        };

        explicit Kh02MapSession(Hooks hooks);
        ~Kh02MapSession();
        void observe_world(RC::Unreal::UObject* world);
        void bind_source_script(RC::Unreal::UObject* script);
        void invalidate_source_script();
        auto generation() const -> Generation;
        auto accepts(Generation generation) const -> bool;
        auto accepts_continuation(Generation generation, RC::Unreal::UObject* world) const -> bool;
        auto owns_world(RC::Unreal::UObject* world) const -> bool;
        void arm(RC::Unreal::UObject* context, const std::wstring& target);
        void begin_arrival(RC::Unreal::UObject* world);
        void source_loading_ready(RC::Unreal::UObject* script);
        void request_entry_fade(RC::Unreal::UObject* script);
        void observe_nonlatent_arrival_fade();
        void notify_readiness();
        void tick(RC::Unreal::UObject* pawn);
        void cancel() noexcept;
        auto active() const -> bool;
        auto world() const -> RC::Unreal::UObject*;
        auto phase() const -> Kh02ArrivalPhase;
        auto failure() const -> const std::string&;
        auto proof() -> nlohmann::json&;
        auto snapshot() const -> nlohmann::json;
        static auto loading_closed() -> bool;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
