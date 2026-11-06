#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <cstdint>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::garden_return_detail
{
    inline constexpr int32_t check_action_command = 79;
    struct AlternateInteractionConfig
    {
        std::function<bool()> camp_ready;
        std::function<bool()> navigation_pending;
        std::function<void()> select_aqua;
        std::function<void(std::wstring_view)> set_phase;
        std::function<void(std::chrono::steady_clock::time_point)> defer_character_restore;
    };

    struct AlternateInteractionSnapshot
    {
        bool save_pending{};
        bool chest_pending{};
        bool chest_observing{};
        bool collectible_pending{};
        uint64_t saves_opened{};
        uint64_t chests_opened{};
        uint64_t collectibles_collected{};
        std::wstring mirror_rejection;
    };

    // Owns only the unfinished native save/chest interactions and their weak
    // context. Updates are idle until a real input press starts an operation.
    class AlternateInteractionService
    {
      public:
        explicit AlternateInteractionService(AlternateInteractionConfig config);
        ~AlternateInteractionService();
        bool route_save_key(int controller, std::wstring_view key, int event);
        void tick_save(std::chrono::steady_clock::time_point now);
        void tick_interaction(std::chrono::steady_clock::time_point now);
        bool save_pending() const;
        bool chest_pending() const;
        RC::Unreal::UObject* current_save_point(RC::Unreal::UObject* pawn);
        int32_t selected_ship_command(RC::Unreal::UObject* actor, RC::Unreal::UObject* reactor,
                                      RC::Unreal::UObject* pawn, bool require_active = true);
        AlternateInteractionSnapshot snapshot() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace kh3ap::garden_return_detail
