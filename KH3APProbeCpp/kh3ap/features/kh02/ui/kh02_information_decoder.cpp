#include "kh02_information_decoder.h"
#include "kh02_information_operand.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Property/FStructProperty.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto kh02_decode_information(FFrame& stack) -> std::optional<Kh02InformationMessage>
    {
        auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
        if (!frame->Code || !is_uobject_valid_fast(frame->Node) ||
            !frame->Node->GetPathName().starts_with(L"/Game/Zevels/dw/"))
        {
            return std::nullopt;
        }
        auto& script = frame->Node->GetScript();
        auto* operand = frame->Code;
        auto* end = script.GetData() + script.Num();
        // 0.2 supplies only a local TresLocText and AutoClose. KH3 added
        // OptionalDisplayTime; preserve its other callers and signatures.
        if (operand < script.GetData() || operand > end)
        {
            return std::nullopt;
        }
        const auto decoded = kh02_information_operand({operand, static_cast<std::size_t>(end - operand)});
        if (!decoded)
        {
            return std::nullopt;
        }
        auto* local = reinterpret_cast<FProperty*>(decoded->local_property);
        auto* property = reinterpret_cast<UObject*>(local);
        auto* world = kh02_actor_world(frame->Object);
        if (!frame->Locals || !is_uobject_valid_fast(property) || property->GetOuterPrivate() != frame->Node ||
            !local->IsA<FStructProperty>() || local->GetOffset_Internal() < 0 ||
            local->GetOffset_Internal() + local->GetElementSize() > frame->Node->GetPropertiesSize() ||
            !is_uobject_valid_fast(world) || !world->GetPathName().starts_with(L"/Game/Maps/APKH02"))
        {
            return std::nullopt;
        }
        auto* schema = static_cast<FStructProperty*>(local)->GetStruct();
        if (!is_uobject_valid_fast(schema) || schema->GetPathName() != L"/Script/TresGame.TresLocText" ||
            local->GetElementSize() != schema->GetPropertiesSize())
        {
            return std::nullopt;
        }

        return Kh02InformationMessage{world,
                                      schema,
                                      local->ContainerPtrToValuePtr<void>(frame->Locals),
                                      frame->Node->GetPathName(),
                                      decoded->auto_close,
                                      operand + kh02_information_operand_bytes};
    }
} // namespace kh3ap
