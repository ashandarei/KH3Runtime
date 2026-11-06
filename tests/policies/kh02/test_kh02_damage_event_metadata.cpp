#include "kh3ap/features/kh02/challenges/kh02_damage_event_metadata.h"
#include <cassert>
#include <climits>
#include <iostream>

int main()
{
    using kh3ap::kh02_classify_damage_event;
    using kh3ap::Kh02DamageEventMetadata;

    // Each supported element accepts only its own four native spell commands.
    for (int element = 1; element <= 3; ++element)
    {
        const int first_command = 29 + 4 * (element - 1);
        for (int command = first_command - 1; command <= first_command + 4; ++command)
        {
            const auto classified = kh02_classify_damage_event({command, element, 1, L"Magic"}, false);
            assert(classified.magic == (command >= first_command && command < first_command + 4));
        }
    }
    assert(!kh02_classify_damage_event({29, 1, 0, L"Magic"}, false).magic);
    assert(!kh02_classify_damage_event({29, 1, 2, L"Magic"}, false).magic);
    assert(kh02_classify_damage_event({29, 1, 3, L"Magic"}, false).magic);
    assert(!kh02_classify_damage_event({29, 0, 1, L"Magic"}, false).magic);
    assert(!kh02_classify_damage_event({29, INT_MAX, 1, L"Magic"}, false).magic);
    assert(!kh02_classify_damage_event({33, 1, 1, L"Magic"}, false).magic);

    for (int command : {5, 6, 7, 214, 215, 216, 217, 218})
    {
        assert(kh02_classify_damage_event({command, 0, 0, L"Counter"}, false).counter ==
               (command == 6 || (command >= 215 && command <= 217)));
    }

    const Kh02DamageEventMetadata finish{167, 0, 0, L"SCAQ_FIN_01"};
    const auto aqua_finish = kh02_classify_damage_event(finish, true);
    assert(aqua_finish.style && aqua_finish.spellweaver);
    const auto sora_finish = kh02_classify_damage_event(finish, false);
    assert(!sora_finish.style && !sora_finish.spellweaver);
    const auto ordinary_style = kh02_classify_damage_event({166, 0, 0, L"SCAQ_FIN_01"}, true);
    assert(ordinary_style.style && !ordinary_style.spellweaver);
    const auto wrong_attack = kh02_classify_damage_event({167, 0, 0, L"SORA_FIN_01"}, true);
    assert(!wrong_attack.style && !wrong_attack.spellweaver);
    for (const auto* family : {L"SCDW", L"SCHM", L"SCYO", L"SCBZ", L"SCDR", L"SCSK", L"SCGM", L"SCSW",
                               L"SCWA", L"SCSH", L"SCCL", L"SCHL", L"SCFL", L"SCTF", L"SCDF", L"SCLF"})
    {
        const auto attack = std::wstring(family) + L"_ATTACK1";
        const auto sora_style = kh02_classify_damage_event({1, 0, 0, attack}, false);
        assert(sora_style.style && !sora_style.spellweaver);
        assert(!kh02_classify_damage_event({1, 0, 0, attack}, true).style);
    }
    for (const auto* attack : {L"ATTACK1", L"SCAQ_ATTACK1", L"SCA2_ATTACK1", L"SC_UNKNOWN", L"SCDW"})
    {
        assert(!kh02_classify_damage_event({1, 0, 0, attack}, false).style);
    }
    std::cout << "KH0.2 damage metadata classification passed\n";
}
