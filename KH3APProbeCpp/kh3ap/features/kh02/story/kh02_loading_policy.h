#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace kh3ap
{
    namespace kh02_castle_loading
    {
        inline constexpr std::size_t delay_site_offset = 30'506;
        inline constexpr std::size_t delay_site_bytes = 76;
        inline constexpr std::size_t duration_operand_offset = 33;
        inline constexpr int texture_wait_seconds = 3;
        inline constexpr int ready_continuation = 7'860;
    } // namespace kh02_castle_loading

    // The playable Castle Town entry node: Delay(3), "Texture wait finished",
    // then KH02_LoadReady. Shared by fresh checkpoint-102 entry and revisits;
    // cinematic dispatchers and boss Retry are separate call paths.
    inline auto kh02_castle_revisit_delay(std::span<const uint8_t> script, uintptr_t kismet, uintptr_t delay)
        -> std::optional<size_t>
    {
        constexpr auto site = kh02_castle_loading::delay_site_offset;
        if (!kismet || !delay || script.size() < site + kh02_castle_loading::delay_site_bytes)
        {
            return std::nullopt;
        }
        const auto* p = script.data() + site;
        auto value = [&]<class T>(size_t offset)
        {
            T v;
            std::memcpy(&v, p + offset, sizeof(v));
            return v;
        };
        if (p[0] != 0x19 || p[1] != 0x20 || value.operator()<uintptr_t>(2) != kismet ||
            value.operator()<uint32_t>(10) != 54 || value.operator()<uint64_t>(14) != 0 || p[22] != 0x1c ||
            value.operator()<uintptr_t>(23) != delay || p[31] != 0x17 || p[32] != 0x1e ||
            value.operator()<float>(kh02_castle_loading::duration_operand_offset) !=
                kh02_castle_loading::texture_wait_seconds ||
            p[37] != 0x2f || value.operator()<uint32_t>(46) != 32 || p[50] != 0x5b ||
            value.operator()<int32_t>(51) != kh02_castle_loading::ready_continuation || p[55] != 0x1d ||
            value.operator()<int32_t>(56) != 279349679 || p[60] != 0x21 || p[73] != 0x17 || p[74] != 0x30 ||
            p[75] != 0x16)
        {
            return std::nullopt;
        }
        return site + kh02_castle_loading::duration_operand_offset;
    }

    // One synchronous source dispatch. Never retain a bytecode pointer between frames.
    class Kh02CastleDelayOverride
    {
      public:
        Kh02CastleDelayOverride() = default;
        Kh02CastleDelayOverride(const Kh02CastleDelayOverride&) = delete;
        auto operator=(const Kh02CastleDelayOverride&) -> Kh02CastleDelayOverride& = delete;

        ~Kh02CastleDelayOverride()
        {
            if (operand)
            {
                std::memcpy(operand, &original, sizeof(original));
            }
        }

        auto apply(std::span<uint8_t> script, uintptr_t kismet, uintptr_t delay) -> bool
        {
            if (operand)
            {
                return false;
            }
            const auto offset = kh02_castle_revisit_delay(script, kismet, delay);
            if (!offset)
            {
                return false;
            }
            operand = script.data() + *offset;
            std::memcpy(&original, operand, sizeof(original));
            constexpr float next_tick_seconds = 0.f;
            std::memcpy(operand, &next_tick_seconds, sizeof(next_tick_seconds));
            return true;
        }

      private:
        uint8_t* operand{};
        float original{};
    };
} // namespace kh3ap
