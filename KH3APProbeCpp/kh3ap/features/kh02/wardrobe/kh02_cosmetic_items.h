#pragma once

// Names and asset identities from the original 0.2 wardrobe data. No asset payloads.
namespace kh3ap::kh02cosmetics
{
    struct Item
    {
        int category;
        const wchar_t* asset;
        const wchar_t* title;
    };

    inline constexpr Item items[]{
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head011.DP_Head011", L"Tiara"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head012.DP_Head012", L"Royal Tiara"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head013.DP_Head013", L"Venus's Tiara"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head021.DP_Head021", L"Antennae"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head022.DP_Head022", L"Pulse Antennae"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head023.DP_Head023", L"Cyber Antennae"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head031.DP_Head031", L"Marie"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head032.DP_Head032", L"Figaro"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head033.DP_Head033", L"Cheshire Cat"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head041.DP_Head041", L"Minnie Ears (Red Bow)"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head042.DP_Head042", L"Minnie Ears (Blue Bow)"},
        {0, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Head043.DP_Head043", L"Minnie Ears (White Bow)"},
        {1, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Arm011.DP_Arm011", L"Pauldron"},
        {1, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Arm012.DP_Arm012", L"Mystic Pauldron"},
        {1, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Arm013.DP_Arm013", L"Royal Pauldron"},
        {1, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Arm021.DP_Arm021", L"Arm Plate"},
        {1, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Arm022.DP_Arm022", L"Warrior's Arm Plate"},
        {1, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Arm023.DP_Arm023", L"Voltaic Arm Plate"},
        {1, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Arm031.DP_Arm031", L"Arm Guards"},
        {1, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Arm032.DP_Arm032", L"Lustrous Arm Guards"},
        {1, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Arm033.DP_Arm033", L"Flawless Arm Guards"},
        {2, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Body011.DP_Body011", L"Iron Ornament"},
        {2, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Body012.DP_Body012", L"Radiant Ornament"},
        {2, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Body013.DP_Body013", L"Astral Ornament"},
        {2, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Body021.DP_Body021", L"Blades"},
        {2, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Body022.DP_Body022", L"Pulse Blades"},
        {2, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Body023.DP_Body023", L"Cyber Blades"},
        {2, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Body031.DP_Body031", L"Wings"},
        {2, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Body032.DP_Body032", L"Lustrous Wings"},
        {2, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_Body033.DP_Body033", L"Flawless Wings"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP000.DP_TexP000", L"Plain"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP011.DP_TexP011", L"Classy (Yellow)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP012.DP_TexP012", L"Classy (Blue)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP013.DP_TexP013", L"Classy (White)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP021.DP_TexP021", L"Mecha (Pink)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP022.DP_TexP022", L"Mecha (Yellow)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP023.DP_TexP023", L"Mecha (Blue)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP031.DP_TexP031", L"Grace (Blue)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP032.DP_TexP032", L"Grace (Purple)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP033.DP_TexP033", L"Grace (Pink)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP041.DP_TexP041", L"Diamond (White)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP042.DP_TexP042", L"Diamond (Blue)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP043.DP_TexP043", L"Diamond (Green)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP051.DP_TexP051", L"Coil (White)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP052.DP_TexP052", L"Coil (Yellow)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP053.DP_TexP053", L"Coil (Pink)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP061.DP_TexP061", L"Stitching"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP071.DP_TexP071", L"Crisscross"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP081.DP_TexP081", L"Lace (Crystal)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP091.DP_TexP091", L"Lace (Floral)"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP101.DP_TexP101", L"Polka Dots"},
        {3, L"/Game/Zlueprints/Player/p_ex002/DecoParts/DP_TexP111.DP_TexP111", L"Ribbons"},
    };
} // namespace kh3ap::kh02cosmetics
