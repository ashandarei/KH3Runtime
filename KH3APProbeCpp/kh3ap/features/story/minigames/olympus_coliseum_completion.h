#include "kh3ap/bridge/transport/bridge_channels.h"
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    inline constexpr std::string_view runtime_olympus_coliseum_slot_option =
            "olympus_coliseum_pool";
    inline constexpr int64_t runtime_olympus_coliseum_randomize_option = 2;
    inline constexpr int64_t runtime_olympus_coliseum_junk_option = 3;
    inline constexpr auto runtime_olympus_coliseum_option_is_active(
            int64_t option_mode) noexcept -> bool
    {
        return option_mode == runtime_olympus_coliseum_randomize_option ||
               option_mode == runtime_olympus_coliseum_junk_option;
    }
    inline constexpr std::wstring_view runtime_olympus_coliseum_state_filename =
            channels::runtime_olympus_coliseum_state_file;
    inline constexpr std::wstring_view runtime_olympus_coliseum_progress_save_source =
            L"ColiseumMod_Slot*.sav:CloseHandle";
    inline constexpr std::string_view
            runtime_olympus_coliseum_difficulty_beaten_property =
                    "DifficultyBeaten_12_872D53214D051B78E220FBBBC4837FA7";

    struct RuntimeOlympusColiseumCup
    {
        std::wstring_view cup_id{};
        std::wstring_view series_name{};
        std::wstring_view cup_name{};
        std::wstring_view display_name{};
    };

    inline constexpr std::array runtime_olympus_coliseum_cups{
            RuntimeOlympusColiseumCup{L"series_kh1/cup_phil", L"series_kh1", L"cup_phil", L"KH1 - Phil Cup"},
            RuntimeOlympusColiseumCup{L"series_kh1/cup_pegasus", L"series_kh1", L"cup_pegasus", L"KH1 - Pegasus Cup"},
            RuntimeOlympusColiseumCup{L"series_kh1/cup_hercules", L"series_kh1", L"cup_hercules", L"KH1 - Hercules Cup"},
            RuntimeOlympusColiseumCup{L"series_kh1/cup_hades", L"series_kh1", L"cup_hades", L"KH1 - Hades Cup"},
            RuntimeOlympusColiseumCup{L"series_kh1/cup_gold", L"series_kh1", L"cup_gold", L"KH1 - Gold Match"},
            RuntimeOlympusColiseumCup{L"series_kh1/cup_platinum", L"series_kh1", L"cup_platinum", L"KH1 - Platinum Match"},
            RuntimeOlympusColiseumCup{L"series_kh2/cup_painandpanic", L"series_kh2", L"cup_painandpanic", L"KH2 - Pain and Panic Cup"},
            RuntimeOlympusColiseumCup{L"series_kh2/cup_cerberus", L"series_kh2", L"cup_cerberus", L"KH2 - Cerberus Cup"},
            RuntimeOlympusColiseumCup{L"series_kh2/cup_titan", L"series_kh2", L"cup_titan", L"KH2 - Titan Cup"},
            RuntimeOlympusColiseumCup{L"series_kh2/cup_goddessoffate", L"series_kh2", L"cup_goddessoffate", L"KH2 - Goddess of Fate Cup"},
            RuntimeOlympusColiseumCup{L"series_kh2/cup_paradox", L"series_kh2", L"cup_paradox", L"KH2 - Paradox Cup"},
            RuntimeOlympusColiseumCup{L"series_kh3/cup_hermes", L"series_kh3", L"cup_hermes", L"KH3 - Hermes Cup"},
            RuntimeOlympusColiseumCup{L"series_kh3/cup_apollo", L"series_kh3", L"cup_apollo", L"KH3 - Apollo Cup"},
            RuntimeOlympusColiseumCup{L"series_kh3/cup_athena", L"series_kh3", L"cup_athena", L"KH3 - Athena Cup"},
            RuntimeOlympusColiseumCup{L"series_kh3/cup_zeus", L"series_kh3", L"cup_zeus", L"KH3 - Zeus Cup"},
            RuntimeOlympusColiseumCup{L"series_apro/cup_chef", L"series_apro", L"cup_chef", L"Apro - Chef Cup"},
            RuntimeOlympusColiseumCup{L"series_apro/cup_flan", L"series_apro", L"cup_flan", L"Apro - Flan Cup"},
            RuntimeOlympusColiseumCup{L"series_apro/cup_story", L"series_apro", L"cup_story", L"Apro - The Story So Far Cup"},
            RuntimeOlympusColiseumCup{L"series_apro/cup_infernal", L"series_apro", L"cup_infernal", L"Apro - Infernal Cup"},
            RuntimeOlympusColiseumCup{L"series_apro/cup_limitcut", L"series_apro", L"cup_limitcut", L"Apro - Limit Cut Cup"},
            RuntimeOlympusColiseumCup{L"series_apro/cup_vessels", L"series_apro", L"cup_vessels", L"Apro - Vessels of Darkness Cup"},
            RuntimeOlympusColiseumCup{L"series_bbs/cup_terra", L"series_bbs", L"cup_terra", L"BBS - Terra Cup"},
            RuntimeOlympusColiseumCup{L"series_bbs/cup_ventus", L"series_bbs", L"cup_ventus", L"BBS - Ventus Cup"},
            RuntimeOlympusColiseumCup{L"series_bbs/cup_aqua", L"series_bbs", L"cup_aqua", L"BBS - Aqua Cup"},
    };

    namespace olympus_coliseum_save_detail
    {
        inline auto read_int32_le(std::string_view bytes, size_t offset)
                -> std::optional<int32_t>
        {
            if (offset > bytes.size() || bytes.size() - offset < sizeof(int32_t))
            {
                return std::nullopt;
            }
            uint32_t raw{};
            std::memcpy(&raw, bytes.data() + offset, sizeof(raw));
            return static_cast<int32_t>(raw);
        }

        inline auto read_fstring(
                std::string_view bytes,
                size_t& offset) -> std::optional<std::string_view>
        {
            const auto length = read_int32_le(bytes, offset);
            if (!length.has_value() || *length <= 0)
            {
                return std::nullopt;
            }
            offset += sizeof(int32_t);
            const size_t byte_length = static_cast<size_t>(*length);
            if (offset > bytes.size() || bytes.size() - offset < byte_length ||
                bytes[offset + byte_length - 1] != '\0')
            {
                return std::nullopt;
            }
            const std::string_view value = bytes.substr(offset, byte_length - 1);
            offset += byte_length;
            return value;
        }

        inline auto read_bool_property_at(
                std::string_view bytes,
                size_t property_name_offset) -> std::optional<bool>
        {
            if (property_name_offset < sizeof(int32_t))
            {
                return std::nullopt;
            }
            size_t cursor = property_name_offset - sizeof(int32_t);
            const auto property_name = read_fstring(bytes, cursor);
            if (!property_name.has_value() ||
                *property_name != runtime_olympus_coliseum_difficulty_beaten_property)
            {
                return std::nullopt;
            }
            const auto property_type = read_fstring(bytes, cursor);
            if (!property_type.has_value() || *property_type != "BoolProperty")
            {
                return std::nullopt;
            }

            // The Olympus Coliseum save uses UE4's serialized FPropertyTag
            // layout: int32 payload size, int32 array index, then the
            // BoolProperty value byte stored in the tag itself.
            constexpr size_t tag_numbers_size = sizeof(int32_t) + sizeof(int32_t);
            if (cursor > bytes.size() ||
                bytes.size() - cursor < tag_numbers_size + 1)
            {
                return std::nullopt;
            }
            cursor += tag_numbers_size;
            const unsigned char value =
                    static_cast<unsigned char>(bytes[cursor]);
            if (value > 1)
            {
                return std::nullopt;
            }
            return value != 0;
        }

        inline auto narrow_ascii(std::wstring_view value) -> std::string
        {
            std::string result{};
            result.reserve(value.size());
            for (const wchar_t character : value)
            {
                if (character < 0 || character > 0x7f)
                {
                    return {};
                }
                result.push_back(static_cast<char>(character));
            }
            return result;
        }
    }

    inline auto runtime_olympus_coliseum_save_has_completed_cup(
            std::string_view save_bytes,
            const RuntimeOlympusColiseumCup& cup) -> bool
    {
        using namespace olympus_coliseum_save_detail;

        const std::string entry_prefix =
                narrow_ascii(cup.series_name) + "_" +
                narrow_ascii(cup.cup_name) + "_";
        if (entry_prefix.size() < 3)
        {
            return false;
        }

        size_t search_from{};
        while (search_from < save_bytes.size())
        {
            const size_t entry_at = save_bytes.find(entry_prefix, search_from);
            if (entry_at == std::string_view::npos)
            {
                return false;
            }

            // A serialized HighScoreStruct is small. Bound the property search
            // to this map entry so another cup cannot satisfy it accidentally.
            const size_t next_entry = save_bytes.find("series_", entry_at + 1);
            const size_t bounded_end = std::min(
                    save_bytes.size(),
                    entry_at + static_cast<size_t>(2048));
            const size_t entry_end = next_entry == std::string_view::npos
                    ? bounded_end
                    : std::min(bounded_end, next_entry);
            const size_t property_at = save_bytes.find(
                    runtime_olympus_coliseum_difficulty_beaten_property,
                    entry_at + entry_prefix.size());
            if (property_at != std::string_view::npos &&
                property_at < entry_end)
            {
                const auto value = read_bool_property_at(
                        save_bytes,
                        property_at);
                if (value == true)
                {
                    return true;
                }
            }
            search_from = entry_at + entry_prefix.size();
        }
        return false;
    }

    inline auto runtime_olympus_coliseum_completed_cup_indices(
            std::string_view save_bytes) -> std::vector<size_t>
    {
        std::vector<size_t> result{};
        if (!save_bytes.starts_with("GVAS"))
        {
            return result;
        }
        for (size_t index = 0;
             index < runtime_olympus_coliseum_cups.size();
             ++index)
        {
            if (runtime_olympus_coliseum_save_has_completed_cup(
                        save_bytes,
                        runtime_olympus_coliseum_cups[index]))
            {
                result.push_back(index);
            }
        }
        return result;
    }

    class RuntimeOlympusColiseumCompletionTracker
    {
    public:
        auto mark_completed(size_t cup_index) noexcept -> bool
        {
            if (cup_index >= completed_.size() || completed_[cup_index])
            {
                return false;
            }
            completed_[cup_index] = true;
            return true;
        }

        auto mark_completed(std::wstring_view cup_id) noexcept -> bool
        {
            for (size_t index = 0; index < runtime_olympus_coliseum_cups.size(); ++index)
            {
                if (runtime_olympus_coliseum_cups[index].cup_id == cup_id)
                {
                    return mark_completed(index);
                }
            }
            return false;
        }

        [[nodiscard]] auto completed_count() const noexcept -> size_t
        {
            size_t result{};
            for (const bool completed : completed_)
            {
                result += completed ? 1U : 0U;
            }
            return result;
        }

        [[nodiscard]] auto completed_cup_ids() const
                -> std::vector<std::wstring_view>
        {
            std::vector<std::wstring_view> result{};
            result.reserve(completed_count());
            for (size_t index = 0; index < completed_.size(); ++index)
            {
                if (completed_[index])
                {
                    result.push_back(runtime_olympus_coliseum_cups[index].cup_id);
                }
            }
            return result;
        }

        auto reset() noexcept -> void
        {
            completed_.fill(false);
        }

    private:
        std::array<bool, runtime_olympus_coliseum_cups.size()> completed_{};
    };
}
