#include "kh3ap/features/music/shuffle/goa_music_patch.h"
#include "kh3ap/features/music/shuffle/goa_music_bytecode.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>
#include <mutex>

namespace kh3ap
{
    auto try_apply_runtime_goa_music_patch(RC::Unreal::UObject* level_script_actor) -> bool
    {
        using namespace RC::Unreal;
        if (!is_uobject_valid_fast(level_script_actor)) return false;
        auto* function = level_script_actor->GetFunctionByNameInChain(STR("ExecuteUbergraph_GoA_01"));
        if (!is_uobject_valid_fast(function)) return false;
        // The CDO and live actor share the function. No retained raw objects,
        // rooted replacements, or dependency on whether a shuffled asset loaded.
        static std::mutex patch_mutex;
        const std::lock_guard lock{patch_mutex};
        auto& script = function->GetScript();
        const size_t size = script.Num() > 0 ? static_cast<size_t>(script.Num()) : 0;
        const size_t silenced = silence_goa_music_constants(
            std::span<uint8_t>{script.GetData(), size}, [](uintptr_t address) {
                auto* object = reinterpret_cast<UObject*>(address);
                return is_uobject_valid_fast(object) && object->GetPathName() == runtime_goa_music_path;
            });
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] GoA music patch | object={} silenced={} owner=Field script_size={}\n"),
            level_script_actor->GetName(), silenced, size);
        return true;
    }
}
