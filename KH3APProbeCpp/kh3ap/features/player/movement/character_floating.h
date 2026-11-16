#pragma once
namespace RC::Unreal { class UObject; }
namespace kh3ap::character_floating
{
    bool initialize();
    bool allows_swap(RC::Unreal::UObject* pawn);
    void prepare_freefall(RC::Unreal::UObject* world);
    bool restore_camera_view(RC::Unreal::UObject* pawn);
}
