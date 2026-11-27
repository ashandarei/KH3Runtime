#pragma once

#include <array>
#include <cstddef>

namespace RC::Unreal
{
    class UClass;
    class UObject;
}

namespace kh3ap::toy_box_ufo
{
    struct alignas(16) DamageInfoCopy
    {
        std::array<std::byte, 128> bytes;
    };

    const void* damage_info_for_hit(RC::Unreal::UClass* hit_counter_owner, RC::Unreal::UObject* controller,
                                    RC::Unreal::UObject* causer, const void* info, DamageInfoCopy& copy);
}
