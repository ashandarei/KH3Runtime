#pragma once

#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    enum class NativeCutsceneSkipResult
    {
        Requested,
        Busy,
        Unavailable,
        Unsupported
    };

    // The same UI-manager entry point bound to the normal cutscene Skip delegate.
    // It owns the fade and deferred world-wide OnEventSkip dispatch. No pause
    // menu, input events, Blueprint End_skip guesses, or destination overrides.
    auto request_native_cutscene_skip(RC::Unreal::UObject* world) -> NativeCutsceneSkipResult;
    auto native_cutscene_pause_type(RC::Unreal::UObject* world) -> int;
} // namespace kh3ap
