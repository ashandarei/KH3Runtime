#include <span>
#include <utility>
#include "kh3ap/features/moogle_shop/shop_movie_service.h"
#include "kh3ap/features/moogle_shop/shop_movie_patch.h"
#include "kh3ap/features/moogle_shop/shop_movie_patch_variants.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UObject.hpp>

#include <chrono>
#include <exception>

namespace kh3ap
{
    namespace
    {
        constexpr auto shop_movie_load_uuid = 0x4b483350;
        constexpr auto max_shop_movie_bytes = 4 * 1024 * 1024;
        constexpr auto shop_movie_readiness_interval = std::chrono::milliseconds(100);
        constexpr auto shop_movie_load_timeout = std::chrono::seconds(30);
    } // namespace

    auto RuntimeShopMovieService::tick() -> void
    {
        if (complete_)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < next_poll_)
        {
            return;
        }
        next_poll_ = now + shop_movie_readiness_interval;
        using namespace RC;
        using namespace RC::Unreal;
        constexpr auto path = L"/Game/UI/Scaleform/41_shop/Shop.Shop";
        try
        {
            // KH3's UE4.17 Kismet library exposes only the latent loader, not
            // LoadAsset_Blocking. Use the existing loader with a unique UUID.
            if (!requested_)
            {
                auto* context = context_ ? context_() : nullptr;
                if (!is_uobject_valid_fast(context))
                {
                    return;
                }
                requested_ =
                    request_runtime_asset_load(context, path, nullptr, shop_movie_load_uuid);
                if (!requested_)
                {
                    return;
                }
                started_ = now;
                Output::send<LogLevel::Normal>(
                    L"[KH3APProbeCpp] Shop runtime movie preload requested\n");
            }
            auto* asset = find_loaded_runtime_asset(path);
            if (!asset && now - started_ < shop_movie_load_timeout)
            {
                return;
            }
            complete_ = true;
            auto* property = is_uobject_valid_fast(asset)
                                 ? asset->GetPropertyByNameInChain(L"RawData")
                                 : nullptr;
            if (!property || property->GetClass().GetName() != L"ArrayProperty")
            {
                Output::send<LogLevel::Error>(L"[KH3APProbeCpp] Shop runtime movie patch "
                                              L"unavailable: asset/RawData not loaded\n");
                return;
            }
            auto* inner = static_cast<FArrayProperty*>(property)->GetInner();
            if (!inner || inner->GetElementSize() != 1 ||
                inner->GetClass().GetName() != L"ByteProperty")
            {
                return;
            }
            auto* raw = property->ContainerPtrToValuePtr<TArray<uint8_t>>(asset);
            if (!raw || raw->Num() <= 0 || raw->Num() > max_shop_movie_bytes || !raw->GetData())
            {
                return;
            }
            const std::span<const uint8_t> original{raw->GetData(),
                                                    static_cast<size_t>(raw->Num())};
            const auto hash = shop_movie_fingerprint(original);
            std::vector<uint8_t> result;
            const auto variant = decode_supported_shop_movie(original, result);
            if (variant.empty())
            {
                Output::send<LogLevel::Error>(
                    L"[KH3APProbeCpp] Shop runtime movie patch refused unknown movie size={} "
                    L"hash={:016x}; original retained\n",
                    original.size(), hash);
                return;
            }
            TArray<uint8_t> replacement;
            replacement.Append(result.data(), static_cast<int32_t>(result.size()));
            *raw = std::move(replacement);
            // Prevent GC/reloading an unpatched copy after returning to title or
            // changing worlds. No GFx wrapper or unprotected UObject is retained.
            asset->SetRootSet();
            Output::send<LogLevel::Normal>(
                L"[KH3APProbeCpp] Shop runtime movie ready source=original_ap_extension variant={} "
                L"bytes={} elapsed_ms={} asset={}\n",
                variant == "steam" ? L"steam" : L"egs", raw->Num(),
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started_)
                    .count(),
                path);
        }
        catch (const std::exception&)
        {
            complete_ = true;
            Output::send<LogLevel::Error>(
                L"[KH3APProbeCpp] Shop runtime movie patch failed with an exception\n");
        }
    }
} // namespace kh3ap
