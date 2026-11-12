#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::character_swap
{
    auto character_integer_value(RC::Unreal::UObject* pawn, const wchar_t* method) -> int;

    struct CharacterSwapHpBreak
    {
        static auto capture(RC::Unreal::UObject* pawn) -> std::optional<CharacterSwapHpBreak>;
        static void clear_parked(RC::Unreal::UObject* pawn);
        void restore(RC::Unreal::UObject* pawn) const;

        int32_t cap{};
        float seconds{};
        std::chrono::steady_clock::time_point captured_at{};
    };

    // Copies values only. No pointer into the outgoing pawn survives a swap.
    class CharacterSwapStats
    {
    public:
        static auto validate(RC::Unreal::UObject* pawn) -> void;
        static auto totals(RC::Unreal::UObject* pawn) -> std::array<int32_t, 5>;
        auto capture(RC::Unreal::UObject* pawn) -> bool;
        auto apply_base(RC::Unreal::UObject* pawn) const -> void;
        auto synchronize_totals(RC::Unreal::UObject* pawn) const -> void;
        auto add_bonus(RC::Unreal::UObject* pawn, int hp_amount, int mp_amount) -> void;
        auto level() const -> int32_t
        {
            return sora_level;
        }

    private:
        std::array<int32_t, 7> sora_stats{};
        std::array<int32_t, 5> sora_totals{};
        int32_t sora_level{};
    };
} // namespace kh3ap::character_swap
