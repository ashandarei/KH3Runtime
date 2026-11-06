#pragma once

#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    auto try_inject_runtime_sora_normal_form_barrier(
            RC::Unreal::UObject* sora_pawn,
            std::wstring* status_details) -> bool;

    auto try_remove_runtime_sora_normal_form_barrier(
            RC::Unreal::UObject* sora_pawn,
            std::wstring* status_details) -> bool;
}
