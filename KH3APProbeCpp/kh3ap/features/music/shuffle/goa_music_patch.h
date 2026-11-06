#pragma once

#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // The AP mod's original Garden level Blueprint resets the BGM slots on
    // BeginPlay and plays BGM_field_rg03 through
    // GameplayStatics::PlaySound2D, a fire-and-forget sound nothing can
    // redirect or stop afterwards. Suppress that sound before BeginPlay;
    // the music service owns the Garden's Field slot in every mode instead.
    inline constexpr auto runtime_goa_level_script_class = L"GoA_01_C";
    inline constexpr std::wstring_view runtime_goa_music_path =
            L"/Game/Sound/_DLC/BGM/BGM_field_rg03.BGM_field_rg03";

    // Object observer, when the Garden's level script actor is constructed:
    // nulls every EX_ObjectConst naming the Garden track, independently of
    // shuffle settings or replacement-asset readiness. Other sounds remain
    // untouched. Returns true when the object was the Garden's level script.
    auto try_apply_runtime_goa_music_patch(RC::Unreal::UObject* level_script_actor) -> bool;
}
