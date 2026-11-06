#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::garden_return_detail
{
    struct CharacterRestorationConfig
    {
        std::function<bool()> navigation_pending;
        std::function<bool()> interaction_pending;
        std::function<bool()> combat_swap_busy;
        std::function<bool()> camp_ready;
        std::function<bool(RC::Unreal::UObject*, int)> change_player;
        std::function<void(std::wstring_view)> set_phase;
    };

    struct CharacterRestorationSnapshot
    {
        bool aqua_selected{};
        bool pending{true};
        uint64_t context_revision{UINT64_MAX};
        uint64_t checks{};
        uint64_t events{};
        std::chrono::steady_clock::time_point next_attempt{};
    };

    // World/possession revisions wake reconciliation. A successful observation
    // retires the pending work; retry deadlines belong only to unfinished work.
    class CharacterRestoration
    {
      public:
        explicit CharacterRestoration(CharacterRestorationConfig config);
        void select_aqua(bool selected);
        void request(bool selected);
        void defer_until(std::chrono::steady_clock::time_point deadline);
        void tick(std::chrono::steady_clock::time_point now);
        bool finish_arrival(RC::Unreal::UObject* pawn);
        const CharacterRestorationSnapshot& snapshot() const;

      private:
        CharacterRestorationConfig config_;
        CharacterRestorationSnapshot state_;
    };
} // namespace kh3ap::garden_return_detail
