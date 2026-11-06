#pragma once
#include <cstdint>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // One synchronous source map-set request with a checked native latent ABI.
    void kh02_apply_mapset(RC::Unreal::UObject* context, RC::Unreal::UObject* mapset, const wchar_t* command,
                           int32_t request_id);
} // namespace kh3ap
