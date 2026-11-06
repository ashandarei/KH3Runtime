#include "kh02_loading_completion.h"
#include "kh02_story.h"
#include "kh3ap/features/world/loading_runtime.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace loading_runtime;

    RuntimeLoadingCompletion kh02_finish_loading(UObject* pawn)
    {
        if (!Kh02Story::native_save_ready(pawn))
        {
            return {};
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!supported_image(base))
        {
            throw std::runtime_error("kh02_loading_unsupported_image");
        }
        auto* manager = owned_save_manager(pawn);
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        if (!is_uobject_valid_fast(manager) || !is_uobject_valid_fast(controller) ||
            manager->GetClassPrivate()->GetName() != L"TresUIManager" ||
            try_read_reflected_object_property_value(controller, L"Pawn") != pawn ||
            try_read_reflected_object_property_value(manager, L"m_pPlayerController") != controller)
        {
            return {};
        }
        const auto screen = loading_screen_snapshot(base, manager);
        if (screen.layout_mismatch)
        {
            throw std::runtime_error("kh02_loading_screen_layout_mismatch");
        }
        // Normal play leaves Continue to the player's native input. Diagnostic
        // startup/wait requests have their own explicitly authorized handler.
        const auto transition = loading_transition(base, false);
        if (transition.layout_mismatch || transition.retire_failed)
        {
            throw std::runtime_error(transition.retire_failed ? "kh02_loading_continue_task_retire_failed"
                                                              : "kh02_loading_transition_layout_mismatch");
        }
        return {transition.complete, transition.prompt_ready, transition.dispatched};
    }
} // namespace kh3ap
