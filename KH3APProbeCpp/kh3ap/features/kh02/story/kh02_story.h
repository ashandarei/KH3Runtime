#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <functional>
#include "kh3ap/features/kh02/combat/kh02_battle_level_policy.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02Story
    {
      public:
        explicit Kh02Story(std::function<Kh02BattleLevelSettings()> battle_level_settings);
        ~Kh02Story();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto command(RC::Unreal::UObject* pawn, std::wstring_view action) -> std::optional<std::string>;
#endif
        void tick(RC::Unreal::UObject* pawn, bool playable);
        void tick_enemy_level(RC::Unreal::UObject* pawn);
        auto needs_frame() const -> bool;
        void tick_frame(RC::Unreal::UObject* pawn);
        auto prepare_entry(RC::Unreal::UObject* pawn) -> bool;
        void prepare_asset_access();
        auto preparation_pending() const -> bool;
        auto entry_assets_notified() const -> bool;
        void tick_preparation(RC::Unreal::UObject* pawn);
        void leave_world(RC::Unreal::UObject* destination);
        auto active_session() const -> bool;
        auto failure() const -> std::string;
        // Recovery retires owned callbacks and presentation without completing a
        // source graph or changing its progress. Safe exit uses native map travel.
        void retire_failed_session(std::string_view reason);
        auto exit_failed_session(RC::Unreal::UObject* pawn) -> bool;
        auto recovery_garden_ready(RC::Unreal::UObject* pawn) const -> bool;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto movie_snapshot() const -> std::string;
#endif
        static auto challenges_available() -> bool;
        // Borrowed current Aqua only. Never populate the playable-Sora cache.
        // Grants may use this solely to authorize writes to Sora's persistent records.
        static auto persistent_reward_context() -> RC::Unreal::UObject*;
        static auto current_hero() -> RC::Unreal::UObject*;
        // Consumes the existing native award only for the owned 0.2 Aqua.
        static auto award_shared_experience(void* save_owner, int amount, int lane) noexcept -> bool;
        static void refresh_sora_experience(RC::Unreal::UObject* pawn);
        static auto aqua_experience(RC::Unreal::UObject* pawn) -> std::optional<int>;
        static auto map_overlay_hero() -> RC::Unreal::UObject*;
        static auto map_overlay_ready(RC::Unreal::UObject* verified_hero = nullptr) -> bool;
        // Uses the private source bank. Mutations require playable Garden Sora.
        static auto gameflow_command(RC::Unreal::UObject* pawn, std::wstring_view request_id, std::wstring_view command,
                                     std::wstring_view flag_name, std::optional<int> value) -> std::string;
        // Agent manual saves only; does not authorize AP grants or title loading.
        static auto native_save_ready(RC::Unreal::UObject* pawn) -> bool;
        static void release_dressing_room(bool restore_materials);
        static auto open_dressing_room(RC::Unreal::UObject* camp) -> bool;
        static auto open_challenges(RC::Unreal::UObject* camp) -> bool;
        static auto challenges_open() -> bool;
        static auto route_challenges(int controller, std::wstring_view key, int event, bool gamepad) -> bool;
        static auto open_area_select(RC::Unreal::UObject* menu, std::function<void(int)> selected) -> bool;
        static auto travel_to_visited_area(int area) -> bool;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
