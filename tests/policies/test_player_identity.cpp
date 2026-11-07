#include "kh3ap/features/player/character_ids.h"
#include "kh3ap/features/player/player_ids.h"
#include <cassert>
#include <type_traits>

int main()
{
    using namespace kh3ap;
    static_assert(!std::is_convertible_v<PlayerId, CharacterId>);
    static_assert(!std::is_convertible_v<CharacterId, PlayerId>);
    static_assert(!std::is_convertible_v<int, PlayerId>);
    static_assert(!std::is_convertible_v<int, CharacterId>);

    // Native save-record lookups take both IDs. Assert their shipping values
    // separately so a player ID cannot accidentally replace a character kind.
    assert(character_id_value(CharacterId::Sora) == 1);
    assert(player_id_value(PlayerId::Sora) == 0);
    assert(character_id_value(CharacterId::Riku) == 2);
    assert(player_id_value(PlayerId::Riku) == 8);
    assert(character_id_value(CharacterId::Aqua) == 6);
    assert(player_id_value(PlayerId::Aqua) == 10);
    assert(character_id_value(CharacterId::Kairi) == 3);
    assert(player_id_value(PlayerId::Kairi) == 16);
    assert(character_id_value(CharacterId::Roxas) == 7);
    assert(player_id_value(PlayerId::Roxas) == 17);

    assert(is_standard_sora_player_id(player_id_value(PlayerId::SoraCaribbean)));
    assert(is_standard_sora_player_id(player_id_value(PlayerId::SoraKH1)));
    assert(!is_standard_sora_player_id(-1));
    assert(!is_standard_sora_player_id(player_id_value(PlayerId::Riku)));
    assert(!is_standard_sora_player_id(player_id_value(PlayerId::SoraTutorial)));
    assert(!is_standard_sora_player_id(player_id_value(PlayerId::SoraHundredAcreWood)));
    assert(!is_standard_sora_player_id(player_id_value(PlayerId::SoraYozora)));
}
