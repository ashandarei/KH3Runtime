#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace kh3ap::kh02_chest_journal
{
    // These identities own the unused tail of KH3's native journal. Reset and
    // registration must use the same boundary, preserving all preceding bits.
    inline constexpr std::size_t bit_count = 4'096;
    inline constexpr std::size_t bits_per_word = std::numeric_limits<uint32_t>::digits;
    inline constexpr std::size_t word_count = bit_count / bits_per_word;
    inline constexpr int first_imported_chest_bit = 4'056;
    inline constexpr int forest_first_bit = 4'072;
    inline constexpr int mirror_destinations_first_bit = 4'084;
    inline constexpr int world_within_first_bit = 4'089;
    inline constexpr int castle_first_bit = 4'091;
    inline constexpr std::size_t first_owned_word = first_imported_chest_bit / bits_per_word;
    inline constexpr std::size_t last_owned_word = word_count - 1;
    inline constexpr std::size_t owned_word_count = word_count - first_owned_word;
    inline constexpr std::size_t preserved_bits_in_first_word = first_imported_chest_bit % bits_per_word;
    inline constexpr uint32_t preserved_native_mask = (uint32_t{1} << preserved_bits_in_first_word) - 1;

    inline constexpr std::array<std::wstring_view, 5> castle_chests{L"DW_LBOX01", L"DW_BOX003", L"DW_BOX004",
                                                                    L"DW_BOX006", L"DW_BOX007"};
    inline constexpr std::array<std::wstring_view, 2> world_within_chests{L"DW_BOX008", L"DW_LBOX02"};
    inline constexpr std::array<std::wstring_view, 5> mirror_destination_chests{
        L"DW_BOX011", L"DW_BOX013", L"DW_BOX033", L"DW_BOX015", L"DW_BOX016"};
    inline constexpr std::array<std::wstring_view, 12> forest_chests{
        L"DW_BOX017", L"DW_BOX018", L"DW_BOX019", L"DW_BOX020", L"DW_BOX021", L"DW_BOX022",
        L"DW_BOX023", L"DW_BOX024", L"DW_BOX025", L"DW_LBOX03", L"DW_BOX034", L"DW_BOX035"};
    // Later journal bindings for authored chests, not synthetic AP locations.
    // AP eligibility is supplied by the external location catalog:
    // the unreachable dw_01 opening chest has no imported journal binding.
    inline constexpr std::array<std::wstring_view, 16> additional_chests{
        L"DW_BOX009", L"DW_BOX026", L"DW_BOX027", L"DW_BOX028", L"DW_BOX029", L"DW_LBOX04", L"DW_BOX002", L"DW_BOX005",
        L"DW_BOX010", L"DW_BOX012", L"DW_BOX014", L"DW_BOX030", L"DW_BOX031", L"DW_BOX032", L"DW_BOX036", L"DW_BOX037"};

    struct Group
    {
        int first_bit;
        const std::wstring_view* identities;
        std::size_t count;
        const char* conflict_error;
    };

    // Keep native registration order and the caller's diagnostic categories.
    inline constexpr Group groups[]{
        {castle_first_bit, castle_chests.data(), castle_chests.size(), "kh02_chest_journal_slot_conflict"},
        {world_within_first_bit, world_within_chests.data(), world_within_chests.size(),
         "kh02_mirror_chest_journal_slot_conflict"},
        {mirror_destinations_first_bit, mirror_destination_chests.data(), mirror_destination_chests.size(),
         "kh02_destination_chest_journal_slot_conflict"},
        {forest_first_bit, forest_chests.data(), forest_chests.size(), "kh02_forest_chest_journal_slot_conflict"},
        {first_imported_chest_bit, additional_chests.data(), additional_chests.size(),
         "An added 0.2 chest journal index is already occupied."}};

    static_assert(first_imported_chest_bit + additional_chests.size() == forest_first_bit);
    static_assert(forest_first_bit + forest_chests.size() == mirror_destinations_first_bit);
    static_assert(mirror_destinations_first_bit + mirror_destination_chests.size() == world_within_first_bit);
    static_assert(world_within_first_bit + world_within_chests.size() == castle_first_bit);
    static_assert(castle_first_bit + castle_chests.size() == bit_count);
} // namespace kh3ap::kh02_chest_journal
