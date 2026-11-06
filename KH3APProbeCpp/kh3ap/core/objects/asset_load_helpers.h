#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UClass;
    class UObject;
}

namespace kh3ap
{
    // Game-thread scope for callers that own a native latent completion handler.
    // Unrelated asset requests keep their existing completion behavior.
    class RuntimeAssetLoadCallbackScope
    {
        std::wstring_view previous_callback;

      public:
        explicit RuntimeAssetLoadCallbackScope(std::wstring_view callback);
        ~RuntimeAssetLoadCallbackScope();
        RuntimeAssetLoadCallbackScope(const RuntimeAssetLoadCallbackScope&) = delete;
        auto operator=(const RuntimeAssetLoadCallbackScope&) -> RuntimeAssetLoadCallbackScope& = delete;
    };

    // Game-thread-only, nonloading soft-reference resolution through the
    // engine's object hash. Returns nullptr for absent or still-loading assets;
    // no per-asset GUObjectArray scan, package load, or retained asset pointer.
    auto find_loaded_runtime_asset(std::wstring_view asset_path)
            -> RC::Unreal::UObject*;
    auto find_loaded_runtime_asset_class(std::wstring_view class_path)
            -> RC::Unreal::UClass*;

    // Requests a nonblocking soft-object load through Kismet's latent action
    // manager. The caller must invoke this on the game thread with a live
    // world-context UObject and a UUID unique to that callback target.
    auto request_runtime_asset_load(
            RC::Unreal::UObject* world_context_object,
            std::wstring_view asset_path,
            std::wstring* resolution_source = nullptr,
            int32_t latent_uuid = 0x4b483302) -> bool;

    // Requests a nonblocking soft-class load through Kismet's latent action
    // manager. The caller must invoke this on the game thread with a live
    // world-context UObject and a UUID unique to that callback target.
    auto request_runtime_asset_class_load(
            RC::Unreal::UObject* world_context_object,
            std::wstring_view class_path,
            std::wstring* resolution_source = nullptr,
            int32_t latent_uuid = 0x4b483301) -> bool;
}
