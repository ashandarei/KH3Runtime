#include "kh3ap/features/player/controller_abilities/host.h"

#include "kh3ap/app/state.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/combat/character_magic.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace controller_ability_detail;
    auto RuntimeControllerAbilityService::Impl::resolve_controller() const -> UObject*
    {
        // The cached Sora can outlive a map transition. Never apply its
        // command setters while the actual current world is space.
        if (RuntimeSpaceService::is_space_active())
        {
            return nullptr;
        }
        UObject* const pawn = config_.runtime_state.grant_state().pawn_state().object();
        if (!pawn || !kh3ap::is_uobject_valid_fast(pawn))
        {
            return nullptr;
        }
        // Both ownership properties are inherited by the concrete Sora classes.
        UObject* const controller =
            try_read_reflected_object_property_value(pawn, STR("Controller"));
        return controller && kh3ap::is_uobject_valid_fast(controller) &&
                       try_read_reflected_object_property_value(controller, STR("Pawn")) == pawn
                   ? controller
                   : nullptr;
    }

    auto RuntimeControllerAbilityService::Impl::call_setter(UObject* controller,
                                                            size_t setter_index, bool enabled,
                                                            std::wstring* failure_reason) const
        -> bool
    {
        const RuntimeHotPathProfileScope profile{L"controller_abilities:setter"};
        if (setter_index >= setter_paths_.size())
        {
            if (failure_reason)
            {
                *failure_reason = STR("controller ability setter index invalid");
            }
            return false;
        }
        if (!controller || !kh3ap::is_uobject_valid_fast(controller))
        {
            if (failure_reason)
            {
                *failure_reason = STR("runtime controller unavailable");
            }
            return false;
        }

        const std::wstring_view path = setter_paths_[setter_index];
        const auto separator = path.rfind(L':');
        const std::wstring setter_name = separator != std::wstring_view::npos
                                             ? std::wstring{path.substr(separator + 1)}
                                             : std::wstring{};
        UFunction* const function_object =
            setter_name.empty() ? nullptr
                                : controller->GetFunctionByNameInChain(setter_name.c_str());
        if (!function_object || !kh3ap::is_uobject_valid_fast(function_object))
        {
            if (failure_reason)
            {
                *failure_reason =
                    std::format(STR("controller ability setter unavailable path={}"), path);
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()),
                                      std::byte{0});
        if (!write_bool_function_param(
                find_first_function_param_by_class(function_object, {STR("BoolProperty")}),
                params.data(), enabled))
        {
            if (failure_reason)
            {
                *failure_reason =
                    std::format(STR("controller ability InEnable param unavailable path={}"), path);
            }
            return false;
        }

        const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
        auto* pawn = try_read_reflected_object_property_value(controller, L"Pawn");
        const auto meter = setter_index == setter_index::mp_enabled && character_magic::manages_meter(pawn)
                               ? character_magic::read_meter(pawn) : std::nullopt;
        controller->ProcessEvent(function_object, params.empty() ? nullptr : params.data());
        if (meter) { character_magic::write_meter(pawn, *meter); }
        if (config_.debug_logging)
        {
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Controller ability setter path={} "
                                                "enabled={} controller=0x{:X}\n"),
                                            path, enabled ? STR("true") : STR("false"),
                                            reinterpret_cast<uintptr_t>(controller));
        }
        return true;
    }

    auto RuntimeControllerAbilityService::Impl::observe_controller(UObject* controller) -> bool
    {
        UObject* const previous = controller_.Get();
        if (previous == controller)
        {
            return false;
        }
        controller_ = controller;
        applied_states_.reset();
        return true;
    }

    auto RuntimeControllerAbilityService::Impl::setter_needs_update(size_t setter_index,
                                                                    bool desired) const -> bool
    {
        return applied_states_.needs_update(setter_index, desired);
    }
} // namespace kh3ap
