#include "kh3ap/features/player/state/sora_state_catalog.h"

#include <cassert>
#include <optional>

int main()
{
    using namespace kh3ap;

    const RuntimeSoraStateCatalogEntry* const by_id = lookup_runtime_sora_state_by_id(66);
    assert(by_id != nullptr);
    assert(by_id->kind == RuntimeSoraStateKind::Reflect);
    assert(by_id->class_name == L"TresPlayerState_Reflect");
    assert(by_id->safety == RuntimeSoraStateSafety::Ok);

    assert(runtime_sora_state_id(RuntimeSoraStateKind::Reflect) == std::optional<uint8_t>{66});
    assert(runtime_sora_state_class_name(RuntimeSoraStateKind::Reflect) == L"TresPlayerState_Reflect");
    assert(runtime_sora_state_matches_kind(RuntimeSoraStateKind::Reflect, 66, L""));
    assert(runtime_sora_state_matches_kind(
            RuntimeSoraStateKind::Reflect,
            std::nullopt,
            L"TresPlayerState_Reflect"));

    // The aiming and travel phases must both defer DeathLink/grants, while
    // normal airborne movement after airstepping remains eligible.
    for (const auto kind : {RuntimeSoraStateKind::ShootFlow, RuntimeSoraStateKind::AthleticFlow})
    {
        const auto* entry = lookup_runtime_sora_state_by_kind(kind);
        assert(entry != nullptr);
        assert(entry->safety == RuntimeSoraStateSafety::NotOk);
        assert(lookup_runtime_sora_state_by_id(entry->id) == entry);
        assert(lookup_runtime_sora_state_by_class_name(entry->class_name) == entry);
    }
    assert(runtime_sora_state_id(RuntimeSoraStateKind::AthleticFlow) == std::optional<uint8_t>{42});
    assert(runtime_sora_state_class_name(RuntimeSoraStateKind::AthleticFlow) == L"TresPlayerState_AthleticFlow");
    for (const auto kind : {RuntimeSoraStateKind::Idle, RuntimeSoraStateKind::Jump, RuntimeSoraStateKind::AirSlide})
    {
        assert(lookup_runtime_sora_state_by_kind(kind)->safety == RuntimeSoraStateSafety::Ok);
    }

    return 0;
}
