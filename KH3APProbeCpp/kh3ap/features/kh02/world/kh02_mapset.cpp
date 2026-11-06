#include "kh02_mapset.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/compatibility/kh02_compatibility_operands.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    void kh02_apply_mapset(UObject* context, UObject* mapset, const wchar_t* command, int32_t request_id)
    {
        constexpr size_t parameter_limit_bytes = 2'048;
        constexpr int32_t terminal_linkage = -1;
        Kh02ReflectedCall apply(L"/Script/TresGame.Default__TresGameBlueprintLibrary", L"TresApplyMapSet",
                                "kh02_mapset", parameter_limit_bytes);
        apply.object(L"WorldContextObject", context);
        apply.object(L"MapSet", mapset);
        apply.name(L"cmdName", command);
        apply.boolean(L"isOneFrameExecute", true);

        struct Latent
        {
            int32_t linkage;
            int32_t uuid;
            FName execution;
            UObject* callback;
        };

        static_assert(sizeof(Latent) == kh02compat::vm::native_latent_bytes);
        const Latent latent{terminal_linkage, request_id, FName{}, context};
        auto* property = apply.param(L"LatentInfo");
        if (!property || property->GetElementSize() != sizeof(latent))
        {
            throw std::runtime_error("kh02_mapset_latent_layout");
        }
        std::memcpy(property->ContainerPtrToValuePtr<void>(apply.parameters.data()), &latent, sizeof(latent));
        apply.run();
    }
} // namespace kh3ap
