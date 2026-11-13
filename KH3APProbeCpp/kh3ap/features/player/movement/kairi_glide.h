#pragma once
#include <string>

namespace RC::Unreal { class UObject; }

namespace kh3ap::kairi_glide
{
    // Called by the existing grant/possession refresh, never an idle poll.
    bool refresh(RC::Unreal::UObject* pawn, std::wstring* failure);
}
