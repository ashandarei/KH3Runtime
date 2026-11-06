#pragma once
#include <cstdint>
#include <initializer_list>
#include <string_view>
namespace kh3ap
{
    inline constexpr auto minigame_crc_initial_state = 0xffffffff;
    inline constexpr auto minigame_crc_polynomial = 0xedb88320;

    namespace pooh_reward
    {
        inline constexpr int first_gameplay_phase = 5;
        inline constexpr int last_gameplay_phase = 18;
        inline constexpr int result_phase = 19;
        inline constexpr int panacea_stack_limit = 99;
    } // namespace pooh_reward
    // The bonus is inventory-capped. Replays before returning to the field cannot
    // accumulate more than one full stack, just as native battle-item grants cannot.
    struct PoohPanaceaBonus
    {
        bool playing_seen{}, result_seen{}, pending{};
        void observe(bool playing, bool result)
        {
            if (playing && !result)
            {
                playing_seen = true;
                result_seen = false;
            }
            if (playing_seen && result && !result_seen)
            {
                pending = true;
                result_seen = true;
                playing_seen = false;
            }
        }
        void observe_phase(int phase)
        {
            // WinniePuzzleState: 0Ã¢â‚¬â€œ4 prepare/help/countdown, 5Ã¢â‚¬â€œ18 gameplay,
            // 19 result. All score outcomes use the same result phase.
            observe(phase >= pooh_reward::first_gameplay_phase &&
                        phase <= pooh_reward::last_gameplay_phase,
                    phase == pooh_reward::result_phase);
        }
        void leave_game()
        {
            playing_seen = false;
            result_seen = false;
        }
        void paid()
        {
            pending = false;
        }
    };
    // KH3's remuneration keys use the CRC32 of the UTF-16LE key, not its FName id.
    constexpr uint32_t minigame_reward_crc(std::wstring_view key)
    {
        uint32_t crc = minigame_crc_initial_state;
        for (auto ch : key)
        {
            for (unsigned byte : {unsigned(ch) & 255u, (unsigned(ch) >> 8) & 255u})
            {
                crc ^= byte;
                for (int bit = 0; bit < 8; ++bit)
                {
                    crc = (crc >> 1) ^ ((crc & 1) ? minigame_crc_polynomial : 0);
                }
            }
        }
        return ~crc;
    }
    constexpr std::wstring_view minigame_reward_owner(std::wstring_view key)
    {
        struct Game
        {
            std::wstring_view prefix, owner;
        };
        constexpr Game games[]{
            {L"TS_MINIGAME_", L"/Game/Levels/ts/ts_03/umap/ts_03_gameflow."},
            {L"RA_MINIGAME_", L"/Game/Levels/ra/ra_02/umap/ra_02_mission_Dance_After."},
            {L"FZ_MINIGAME_", L"/Game/Levels/fz/fz_03/umap/fz_03_Minigame."},
            {L"BX_MINIGAME_1_", L"/Game/Levels/bx/bx_02/umap/bx_02_FlashTracer_Main."},
            {L"BX_MINIGAME_2_", L"/Game/Levels/bx/bx_02/umap/bx_02_FlashTracer_Minigame02."},
        };
        for (auto& game : games)
        {
            if (key.starts_with(game.prefix))
            {
                auto suffix = key.substr(game.prefix.size());
                if (suffix == L"Rank_A_1st" || suffix == L"Rank_A" || suffix == L"Rank_B" ||
                    suffix == L"Rank_C" || suffix == L"Rank_D" ||
                    (game.prefix == L"FZ_MINIGAME_" && suffix == L"TREASURE"))
                {
                    return game.owner;
                }
            }
        }
        return {};
    }
    constexpr bool minigame_reward_caller_matches(std::wstring_view key,
                                                  std::wstring_view full_name)
    {
        const auto owner = minigame_reward_owner(key);
        const auto separator = full_name.find(L' ');
        if (owner.empty() || separator == std::wstring_view::npos)
        {
            return false;
        }
        const auto path = full_name.substr(separator + 1);
        if (!path.starts_with(owner))
        {
            return false;
        }
        const auto leaf_begin = owner.rfind(L'/') + 1;
        const auto leaf =
            owner.substr(leaf_begin, owner.size() - leaf_begin - std::wstring_view{L"."}.size());
        const auto object = path.substr(owner.size());
        return object.starts_with(leaf) &&
               object.substr(leaf.size()).starts_with(L":PersistentLevel.");
    }
} // namespace kh3ap
