#include "kh3ap/bridge/transport/bridge_channels.h"
#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace kh3ap
{
    inline constexpr std::string_view runtime_classic_kingdom_slot_option =
            "classic_kingdom_checks";
    inline constexpr std::wstring_view runtime_classic_kingdom_state_filename =
            channels::runtime_classic_kingdom_state_file;

    struct RuntimeClassicKingdomGame
    {
        std::wstring_view game_id{};
        std::wstring_view display_name{};
        std::wstring_view score_hook_path{};
    };

    inline constexpr std::array runtime_classic_kingdom_games{
            RuntimeClassicKingdomGame{
                    L"LSIGAME01", L"Giantland",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_Giantland"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME02", L"Mickey, The Mail Pilot",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_TheMailPilot"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME03", L"The Musical Farmer",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_MusicalFarmer"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME04", L"Building a Building",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_BuildingaBuilding"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME05", L"The Mad Doctor",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_MadDoctor"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME06", L"Mickey's Kitten Catch",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_MickeysKittenCatch"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME07", L"The Klondike Kid",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_TheKlondikeKid"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME08", L"Fishin' Frenzy",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_FishinAround"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME09", L"The Karnival Kid",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_TheKarnivalKid"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME10", L"Mickey Cuts Up",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_MickeyCutsUp"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME11", L"Mickey's Prison Escape",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_MickeysPrisonEscape"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME12", L"How to Play Baseball",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_HowtoPlayBaseball"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME13", L"How to Play Golf",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_HowtoPlayGolf"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME14", L"Mickey's Circus",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_MickeysCircus"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME15", L"Camping Out",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_CampingOut"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME16", L"Taxi Troubles",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_TrafficTroubles"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME17", L"Beach Party",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_BeachParty"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME18", L"The Wayward Canary",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_TheWaywardCanary"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME19", L"Mickey's Mechanical Man",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_MickeysMechanicalMan"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME20", L"The Barnyard Battle",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_TheBarnyardBattle"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME21", L"Cast Out to Sea",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_TheCastaway"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME22", L"Backyard Sports",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_BarnyardSports"},
            RuntimeClassicKingdomGame{
                    L"LSIGAME23", L"Mickey Steps Out",
                    L"/Script/TresGame.TresLSIGameActor:OnUpdatedScore_MickeyStepsOut"},
    };

    class RuntimeClassicKingdomCompletionTracker
    {
    public:
        auto mark_high_score(size_t game_index) noexcept -> bool
        {
            if (game_index >= high_scores_.size() || high_scores_[game_index])
            {
                return false;
            }
            high_scores_[game_index] = true;
            return true;
        }

        auto mark_high_score(std::wstring_view game_id) noexcept -> bool
        {
            for (size_t index = 0; index < runtime_classic_kingdom_games.size(); ++index)
            {
                if (runtime_classic_kingdom_games[index].game_id == game_id)
                {
                    return mark_high_score(index);
                }
            }
            return false;
        }

        [[nodiscard]] auto high_score_count() const noexcept -> size_t
        {
            return marked_count(high_scores_);
        }

        [[nodiscard]] auto all_high_scores_complete() const noexcept -> bool
        {
            return high_score_count() == high_scores_.size();
        }

        [[nodiscard]] auto high_score_game_ids() const
                -> std::vector<std::wstring_view>
        {
            return marked_game_ids(high_scores_);
        }

        auto reset() noexcept -> void
        {
            high_scores_.fill(false);
        }

    private:
        static auto marked_count(
                const std::array<bool, runtime_classic_kingdom_games.size()>& values)
                noexcept -> size_t
        {
            size_t count{};
            for (const bool value : values)
            {
                count += value ? 1U : 0U;
            }
            return count;
        }

        static auto marked_game_ids(
                const std::array<bool, runtime_classic_kingdom_games.size()>& values)
                -> std::vector<std::wstring_view>
        {
            std::vector<std::wstring_view> result{};
            result.reserve(marked_count(values));
            for (size_t index = 0; index < values.size(); ++index)
            {
                if (values[index])
                {
                    result.push_back(runtime_classic_kingdom_games[index].game_id);
                }
            }
            return result;
        }

        std::array<bool, runtime_classic_kingdom_games.size()> high_scores_{};
    };
}
