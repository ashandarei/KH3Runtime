#include "kh3ap/core/platform/steam_identity.h"

#include <cassert>

int main()
{
    using namespace kh3ap;

    assert(kh3_steam_shared_save_account_id.size() == 32);
    assert(is_kh3_steam_shared_save_account_id(
            L"00000000000000000000000000000000"));
    assert(!is_kh3_steam_shared_save_account_id(
            L"00000000000000000000000000000001"));
    assert(!is_standard_steam_id64(kh3_steam_shared_save_account_id));
    assert(is_kh3_steam_save_account_component(L"76561202255233023"));
    assert(is_kh3_steam_save_account_component(
            L"00000000000000000000000000000000"));
    assert(!is_kh3_steam_save_account_component(L""));
    assert(!is_kh3_steam_save_account_component(L"11/1"));
    assert(!is_kh3_steam_save_account_component(
            L"111111111111111111111111111111111"));

    const auto forward_slash_root = kh3_steam_save_root_with_shared_account(
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561197960265729/SaveGames");
    assert(forward_slash_root.has_value());
    assert(*forward_slash_root ==
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/00000000000000000000000000000000/SaveGames");

    const auto backslash_root = kh3_steam_save_root_with_shared_account(
            L"C:\\Users\\test\\OneDrive\\Documents\\My Games\\Kingdom Hearts III\\STEAM\\76561197960265729\\savegames\\");
    assert(backslash_root.has_value());
    assert(*backslash_root ==
            L"C:\\Users\\test\\OneDrive\\Documents\\My Games\\Kingdom Hearts III\\STEAM\\00000000000000000000000000000000\\savegames\\");

    const auto already_shared_root = kh3_steam_save_root_with_shared_account(
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/00000000000000000000000000000000/SaveGames");
    assert(already_shared_root.has_value());
    assert(*already_shared_root ==
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/00000000000000000000000000000000/SaveGames");

    const auto alternate_account_root = kh3_steam_save_root_with_account(
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561197960265729/SaveGames",
            L"76561202255233023");
    assert(alternate_account_root.has_value());
    assert(*alternate_account_root ==
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561202255233023/SaveGames");
    const auto shared_to_alternate_account_root = kh3_steam_save_root_with_account(
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/00000000000000000000000000000000/SaveGames",
            L"76561202255233023");
    assert(shared_to_alternate_account_root.has_value());
    assert(*shared_to_alternate_account_root ==
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561202255233023/SaveGames");
    const auto already_alternate_account_root = kh3_steam_save_root_with_account(
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561202255233023/SaveGames",
            L"76561202255233023");
    assert(already_alternate_account_root.has_value());
    assert(*already_alternate_account_root ==
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561202255233023/SaveGames");
    assert(!kh3_steam_save_root_with_account(
                    L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561197960265729/SaveGames",
                    L"../76561202255233023")
                    .has_value());

    const auto randomizer_sidecar_to_alternate_account =
            kh3_steam_save_path_with_account(
                    L"C:\\Users\\test\\Documents\\My Games\\KINGDOM HEARTS III\\Steam\\00000000000000000000000000000000\\SaveGames\\RandomizerEvents.sav",
                    L"76561202255233023");
    assert(randomizer_sidecar_to_alternate_account.has_value());
    assert(*randomizer_sidecar_to_alternate_account ==
            L"C:\\Users\\test\\Documents\\My Games\\KINGDOM HEARTS III\\Steam\\76561202255233023\\SaveGames\\RandomizerEvents.sav");
    const auto nested_vanilla_to_alternate_account = kh3_steam_save_path_with_account(
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561197960265729/SaveGames/kh3sv2/data/KHIII_slot0.bin",
            L"76561202255233023");
    assert(nested_vanilla_to_alternate_account.has_value());
    assert(*nested_vanilla_to_alternate_account ==
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561202255233023/SaveGames/kh3sv2/data/KHIII_slot0.bin");
    const auto alternate_account_save_root = kh3_steam_save_path_with_account(
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561202255233023/SaveGames",
            L"76561202255233023");
    assert(alternate_account_save_root.has_value());
    assert(*alternate_account_save_root ==
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561202255233023/SaveGames");
    assert(!kh3_steam_save_path_with_account(
                    L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/00000000000000000000000000000000/SaveGamesElsewhere/RandomizerEvents.sav",
                    L"76561202255233023")
                    .has_value());
    assert(!kh3_steam_save_path_with_account(
                    L"C:/Users/test/Documents/KINGDOM HEARTS III/Epic Games Store/account/SaveGames/RandomizerEvents.sav",
                    L"76561202255233023")
                    .has_value());
    const auto alternate_account_sidecar_to_shared =
            kh3_steam_save_path_with_shared_account(
                    L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561202255233023/SaveGames/RandomizerEvents.sav");
    assert(alternate_account_sidecar_to_shared.has_value());
    assert(*alternate_account_sidecar_to_shared ==
            L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/00000000000000000000000000000000/SaveGames/RandomizerEvents.sav");

    assert(!kh3_steam_save_root_with_shared_account(
                    L"C:/Users/test/Documents/KINGDOM HEARTS III/Epic Games Store/account/SaveGames")
                    .has_value());
    assert(!kh3_steam_save_root_with_shared_account(
                    L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/machine-specific-fallback/SaveGames")
                    .has_value());
    assert(!kh3_steam_save_root_with_shared_account(
                    L"C:/Users/test/Documents/My Games/KINGDOM HEARTS III/Steam/76561197960265729/Config")
                    .has_value());

    assert(is_standard_steam_id64(L"76561197960265729"));
    assert(is_standard_steam_id64(L"76561202255233023"));
    assert(!is_standard_steam_id64(L"76561197960265728"));
    assert(!is_standard_steam_id64(L"76561202255233024"));
    assert(!is_standard_steam_id64(L"7656119796026572"));
    assert(!is_standard_steam_id64(L"7656119796026572x"));
    assert(!is_standard_steam_id64(L"machine-specific-fallback"));

    return 0;
}
