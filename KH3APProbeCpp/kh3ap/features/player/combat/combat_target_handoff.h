#pragma once
namespace RC::Unreal { class UObject; }
namespace kh3ap::combat_target_handoff
{
    // One swap transaction; references are weak and scoped to its source world.
    bool prepare(RC::Unreal::UObject* outgoing);
    void apply(RC::Unreal::UObject* incoming);
    void finish(RC::Unreal::UObject* incoming);
    void cancel();
}
