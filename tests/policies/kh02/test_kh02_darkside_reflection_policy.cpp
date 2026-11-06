#include "kh3ap/features/kh02/combat/kh02_darkside_reflection_policy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace kh3ap;
    const Kh02DarksideReflectionEvidence reflected{0x201, 255, 255, 0, true, true, true, true, false};
    assert(kh02_darkside_reflection_team(reflected) == 0);
    for (const auto state : {0u, 1u, 0x99u, 0x211u, 0x299u})
    {
        auto evidence = reflected;
        evidence.state = state;
        assert(!kh02_darkside_reflection_team(evidence));
    }
    for (const auto field : {&Kh02DarksideReflectionEvidence::source_forest_context,
                             &Kh02DarksideReflectionEvidence::current_sora_possessed,
                             &Kh02DarksideReflectionEvidence::homing_target_is_owner,
                             &Kh02DarksideReflectionEvidence::ignores_current_sora})
    {
        auto evidence = reflected;
        evidence.*field = false;
        assert(!kh02_darkside_reflection_team(evidence));
    }
    auto evidence = reflected;
    evidence.ignores_owner = true;
    assert(!kh02_darkside_reflection_team(evidence));
    for (const auto team : {-1, 0, 1, 254})
    {
        evidence = reflected;
        evidence.actor_team = team;
        evidence.collision_team = team;
        assert(!kh02_darkside_reflection_team(evidence));
    }
    evidence = reflected;
    evidence.collision_team = 0;
    assert(!kh02_darkside_reflection_team(evidence));
    for (const auto team : {-1, 255, 256})
    {
        evidence = reflected;
        evidence.sora_team = team;
        assert(!kh02_darkside_reflection_team(evidence));
    }
    evidence = reflected;
    evidence.sora_team = 3;
    assert(kh02_darkside_reflection_team(evidence) == 3);
    std::cout << "KH0.2 reflected Darkside projectile attribution policy passed\n";
}
