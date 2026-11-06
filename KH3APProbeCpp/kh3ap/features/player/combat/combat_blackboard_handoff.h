#pragma once
namespace RC::Unreal { class UObject; }
namespace kh3ap::combat_blackboard_handoff
{
    void capture(RC::Unreal::UObject* enemy, RC::Unreal::UObject* outgoing);
    void capture_cameras(RC::Unreal::UObject* outgoing);
    void suspend();
    void apply(RC::Unreal::UObject* incoming);
    void release();
}
