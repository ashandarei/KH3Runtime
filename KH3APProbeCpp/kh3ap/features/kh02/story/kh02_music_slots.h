#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Native SEAD adapter; cue and world-entry decisions remain in StoryMusic.
    class Kh02MusicSlots
    {
      public:
        auto slot(std::wstring_view name) -> RC::Unreal::UObject*;
        auto field_slot() -> RC::Unreal::UObject*;
        void prepare_battle(std::wstring_view battle);
        void reset_source();
        void play(RC::Unreal::UObject* sound, std::wstring_view slot_name, float seek_time, float fade_time);
        auto stop(std::wstring_view requested_track, float fade_time) -> nlohmann::json;
        void retire_source(bool entering_phantom, nlohmann::json& retired);
        void retire_opening(nlohmann::json& retired);
        auto release_opening_event() -> nlohmann::json;
        auto release_scene_field(std::wstring_view field) -> bool;
        auto release_arrival_menu(uint64_t now) -> nlohmann::json;
        void prepare_settings(RC::Unreal::UObject* world, const std::optional<std::wstring>& field,
                              std::wstring_view battle, nlohmann::json& rows);
        void restore_field(RC::Unreal::UObject* world, std::wstring_view track);
        void set_volume(float value, float fade_seconds);
        auto is_suspended(RC::Unreal::UObject* sound) -> bool;
        auto snapshot() const -> nlohmann::json;

        auto source_track() const -> const std::wstring&
        {
            return source_music_track;
        }

      private:
        RC::Unreal::FWeakObjectPtr bgm_slot, source_music_slot;
        std::wstring source_music_track, prepared_battle_track;
    };
} // namespace kh3ap
