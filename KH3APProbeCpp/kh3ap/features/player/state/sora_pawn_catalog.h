#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    enum class RuntimeSoraPawnKind : uint8_t
    {
        Baymax,
        Caribbean,
        YozoraWorld,
        DebugMod,
        FadedFinalWorld,
        KH2,
        Standard,
        OpeningDarkside,
        Monstropolis,
        Pooh,
        Remy,
        ToyStory,
    };

    struct RuntimeSoraPawnCatalogEntry
    {
        RuntimeSoraPawnKind kind;
        std::wstring_view pawn_id;
        std::wstring_view pawn_class_name;
        std::wstring_view description;
    };

    inline constexpr std::array<RuntimeSoraPawnCatalogEntry, 12> k_runtime_sora_pawn_catalog{{
            {RuntimeSoraPawnKind::Baymax, L"p_bx001", L"p_bx001_Pawn_C", L"Sora (Baymax)"},
            {RuntimeSoraPawnKind::Caribbean, L"p_ca001", L"p_ca001_Pawn_C", L"Sora (Caribbean)"},
            {RuntimeSoraPawnKind::YozoraWorld, L"p_dl001", L"p_dl001_Pawn_C", L"Sora (Yozora world)"},
            {RuntimeSoraPawnKind::DebugMod, L"p_zz001", L"p_zz001_Pawn_C", L"Sora (Debug Mod)"},
            {RuntimeSoraPawnKind::FadedFinalWorld, L"p_ew001", L"p_ew001_Pawn_C", L"Sora (Partially Faded Final World Model)"},
            {RuntimeSoraPawnKind::KH2, L"p_ex011", L"p_ex011_Pawn_C", L"Sora (KH2)"},
            {RuntimeSoraPawnKind::Standard, L"p_ex001", L"p_ex001_Pawn_C", L"Sora"},
            {RuntimeSoraPawnKind::OpeningDarkside, L"p_ex005", L"p_ex005_Pawn_C", L"Sora (opening Darkside)"},
            {RuntimeSoraPawnKind::Monstropolis, L"p_mi001", L"p_mi001_Pawn_C", L"Sora (Monstropolis)"},
            {RuntimeSoraPawnKind::Pooh, L"p_po001", L"p_po001_Pawn_C", L"Sora (Pooh)"},
            {RuntimeSoraPawnKind::Remy, L"p_re001", L"p_re001_Pawn_C", L"Sora (Remy)"},
            {RuntimeSoraPawnKind::ToyStory, L"p_ts001", L"p_ts001_Pawn_C", L"Sora (Toy Story)"},
    }};

    inline auto runtime_sora_pawn_catalog_match(std::wstring_view text) -> const RuntimeSoraPawnCatalogEntry*
    {
        for (const RuntimeSoraPawnCatalogEntry& entry : k_runtime_sora_pawn_catalog)
        {
            if (!entry.pawn_id.empty() && text.find(entry.pawn_id) != std::wstring_view::npos)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    inline auto runtime_sora_pawn_catalog_matches_any(
            std::wstring_view first,
            std::wstring_view second = {}) -> bool
    {
        return runtime_sora_pawn_catalog_match(first) || runtime_sora_pawn_catalog_match(second);
    }
}
