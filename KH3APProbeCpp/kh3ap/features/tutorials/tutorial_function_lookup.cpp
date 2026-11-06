#include "tutorial_patch_helpers.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UFunction.hpp>

namespace kh3ap::tutorial_patch
{
    using namespace RC::Unreal;
    [[nodiscard]] auto find_live_function(std::wstring_view function_path) -> UFunction*
    {
        // Resolve through the engine hash, including a cheap miss when a
        // streamed tutorial function is absent. Class-qualified UE4SS
        // FindObject still searches all loaded UFunctions on those misses.
        // The resolver does not retain stale functions across map reloads.
        auto* const function_class = UFunction::StaticClass();
        auto* const object = find_loaded_runtime_asset(function_path);
        UFunction* const function_object = kh3ap::is_uobject_a_fast(object, function_class)
                                               ? static_cast<UFunction*>(object)
                                               : nullptr;
        return kh3ap::is_uobject_a_fast(function_object, function_class) &&
                       is_live_function_patch_target(function_object)
                   ? function_object
                   : nullptr;
    }

} // namespace kh3ap::tutorial_patch
