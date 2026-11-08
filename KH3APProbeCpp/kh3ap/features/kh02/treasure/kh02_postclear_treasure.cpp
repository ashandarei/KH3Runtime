#include "kh02_postclear_treasure.h"
#include "kh02_postclear_treasure_policy.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    void Kh02PostclearTreasure::tick(UObject* pawn)
    {
        auto* current_world = kh02_actor_world(pawn);
        if (world.Get() != current_world)
        {
            world = make_uobject_weak(current_world);
            stream = {};
            package.clear();
            requested = false;
            ready = false;
        }
        if (!is_uobject_valid_fast(current_world) || (ready && is_uobject_valid_fast(stream.Get())))
        {
            return;
        }
        const auto target = kh02_postclear_treasure_package(current_world->GetPathName());
        if (target.empty())
        {
            return;
        }
        constexpr std::size_t parameter_limit_bytes = 256;
        constexpr auto library = L"/Script/Engine.Default__GameplayStatics";
        if (!requested)
        {
            package = target;
            Kh02ReflectedCall find(library, L"GetStreamingLevel", "kh02_postclear_treasure", parameter_limit_bytes);
            find.object(L"WorldContextObject", pawn);
            find.name(L"PackageName", package.c_str());
            find.run();
            auto* level = find.result();
            if (!is_uobject_valid_fast(level) || level->GetOuterPrivate() != current_world)
            {
                throw std::runtime_error("kh02_postclear_treasure_host_layer_missing");
            }
            Kh02ReflectedCall load(library, L"LoadStreamLevel", "kh02_postclear_treasure", parameter_limit_bytes);
            load.object(L"WorldContextObject", pawn);
            load.name(L"LevelName", package.c_str());
            load.boolean(L"bMakeVisibleAfterLoad", true);
            load.boolean(L"bShouldBlockOnLoad", false);

            struct Latent
            {
                int32_t linkage, uuid;
                FName execution;
                UObject* callback;
            };

            constexpr int32_t terminal_linkage = -1;
            constexpr int32_t load_request_id = 0x4b4834d0;
            const Latent latent{terminal_linkage, load_request_id, FName{}, pawn};
            auto* info = load.param(L"LatentInfo");
            if (!info || info->GetElementSize() != sizeof(latent))
            {
                throw std::runtime_error("kh02_postclear_treasure_latent_layout");
            }
            std::memcpy(info->ContainerPtrToValuePtr<void>(load.parameters.data()), &latent, sizeof(latent));
            load.run();
            stream = make_uobject_weak(level);
            requested = true;
        }
        auto* level = stream.Get();
        if (is_uobject_valid_fast(level))
        {
            Kh02ReflectedCall visible(level, L"IsLevelVisible", "kh02_postclear_treasure", parameter_limit_bytes);
            visible.run();
            ready = visible.boolean_result();
        }
    }

    auto Kh02PostclearTreasure::snapshot() const -> nlohmann::json
    {
        return {{"package", to_utf8(package)}, {"requested", requested}, {"ready", ready}};
    }
} // namespace kh3ap
