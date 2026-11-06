#pragma once
#include <string_view>
namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_camera
{
    void capture_sora(RC::Unreal::UObject *pawn, bool capture_field_height);
    void match_field_height(RC::Unreal::UObject *pawn);
    bool prepare(RC::Unreal::UObject *pawn);
    bool route(int controller, std::wstring_view key, int event, bool gamepad);
} // namespace kh3ap::character_camera
