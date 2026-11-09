#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/bridge/session/deathlink_policy.h"

#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/events/keyblade_inventory_events.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/types.h"
#include "kh3ap/core/reflection/owned_function_cache.h"
#include "kh3ap/core/reflection/receiver_function_cache.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr size_t s_game_over_inline_param_bytes = 64;

        struct GameFlagGetterLayout
        {
            std::wstring path;
            FWeakObjectPtr owner;
            ReceiverFunctionCache<> receiver_functions;
            FProperty* flag{};
            FProperty* result{};

            auto resolve(std::wstring_view requested_path, UObject* receiver) -> UFunction*
            {
                const RuntimeHotPathProfileScope profile{L"game_flags:getter_layout"};
                auto* function = static_cast<UFunction*>(owner.Get());
                if (path == requested_path && is_uobject_valid_fast(function)) return function;
                owner = nullptr;
                flag = result = nullptr;
                path = requested_path;
                function = receiver_functions.resolve(receiver, requested_path);
                if (!function)
                {
                    function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, requested_path);
                }
                if (!is_uobject_valid_fast(function)) return nullptr;
                owner = function;
                flag = find_function_param_by_name(function, STR("FlagName"));
                if (!flag) flag = find_function_param_by_name(function, STR("InFlagName"));
                if (!flag) flag = find_first_function_param_by_class(function, {STR("NameProperty")});
                result = find_function_param_by_name(function, STR("ReturnValue"));
                return function;
            }
        };

        auto elapsed_us(
                std::chrono::steady_clock::time_point begin,
                std::chrono::steady_clock::time_point end) -> uint64_t
        {
            return static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                            end - begin)
                            .count());
        }

        auto format_opaque_world_context_address(const UObject* object) -> std::wstring
        {
            // Runtime item grants intentionally treat the cached Sora pawn as opaque.
            // Do not call IsReal, GetName, GetFullName, GetClass, or other UObject APIs here.
            return std::format(STR("0x{:X}"), reinterpret_cast<uintptr_t>(object));
        }

    }

    auto try_call_bp_tres_get_item(
            std::wstring_view runtime_token,
            UObject* world_context_object,
            bool show_info,
            RuntimeState& runtime_state,
            std::wstring_view bp_get_item_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason) -> bool
    {
        const RuntimeHotPathProfileScope call_profile{L"runtime_item_grant:get_item"};
        UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                runtime_state.object_cache(),
                blueprint_library_default_object_path);
        UFunction* const function_object = [&] {
            const RuntimeHotPathProfileScope profile{L"runtime_item_grant:get_item_lookup"};
            // Resolve through the owning library instead of a first-use global
            // UObject scan. Weak class/function identities survive map changes
            // without retaining a pawn or accepting a reused object slot.
            static ReceiverFunctionCache<1> functions;
            if (auto* function = functions.resolve(self_object, bp_get_item_path))
            {
                return function;
            }
            return UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, bp_get_item_path);
        }();
        if (!is_uobject_valid_fast(self_object))
        {
            self_object = try_get_function_default_object(function_object);
        }
        if (!is_uobject_valid_fast(function_object) || !is_uobject_valid_fast(self_object) ||
            !world_context_object || runtime_token.empty())
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("missing prereq function={} self={} world={} token={}"),
                        function_object ? function_object->GetFullName() : std::wstring{STR("<null>")},
                        format_object_debug_name(self_object),
                        format_opaque_world_context_address(world_context_object),
                        runtime_token.empty() ? STR("<empty>") : std::wstring{runtime_token});
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});

        if (FProperty* const token_property = find_function_param_by_name(function_object, STR("InItemKeyName")))
        {
            if (FName* const token_value = token_property->ContainerPtrToValuePtr<FName>(params.data()))
            {
                *token_value = FName(runtime_token, FNAME_Add);
            }
        }
        if (FProperty* const num_property = find_function_param_by_name(function_object, STR("InNum")))
        {
            if (int32_t* const num_value = num_property->ContainerPtrToValuePtr<int32_t>(params.data()))
            {
                *num_value = 1;
            }
        }
        if (FProperty* const world_property = find_function_param_by_name(function_object, STR("WorldContextObject")))
        {
            if (UObject** const world_value = world_property->ContainerPtrToValuePtr<UObject*>(params.data()))
            {
                *world_value = world_context_object;
            }
        }
        if (FProperty* const show_property = find_function_param_by_name(function_object, STR("bShowInfo")))
        {
            if (bool* const show_value = show_property->ContainerPtrToValuePtr<bool>(params.data()))
            {
                *show_value = show_info;
            }
        }

        if (debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Calling BP_TresGetItem token={} | self={} | world={} | show_info={}\n"),
                    runtime_token,
                    format_object_debug_name(self_object),
                    format_opaque_world_context_address(world_context_object),
                    show_info ? STR("true") : STR("false"));
        }

        {
            const RuntimeHotPathProfileScope profile{L"runtime_item_grant:get_item_process_event"};
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            // ProcessEvent is the first operation allowed to consume the cached pawn.
            self_object->ProcessEvent(function_object, params.data());
        }

        notify_runtime_keyblade_inventory_changed(runtime_token);

        if (FProperty* const return_property = find_function_param_by_name(function_object, STR("ReturnValue")); return_property)
        {
            if (bool* const return_value = return_property->ContainerPtrToValuePtr<bool>(params.data()))
            {
                if (!*return_value && failure_reason)
                {
                    *failure_reason = std::format(
                            STR("return=false self={} world={}"),
                            format_object_debug_name(self_object),
                            format_opaque_world_context_address(world_context_object));
                }
                return *return_value;
            }
        }

        return true;
    }

    auto try_call_bp_tres_have_item(
            std::wstring_view runtime_token,
            UObject* world_context_object,
            bool& has_item,
            RuntimeState& runtime_state,
            std::wstring_view bp_have_item_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason) -> bool
    {
        has_item = false;
        UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                runtime_state.object_cache(),
                blueprint_library_default_object_path);
        if (!is_uobject_valid_fast(self_object))
            self_object = find_loaded_runtime_asset(blueprint_library_default_object_path);
        // Map-ready Link reconciliation asks about five Heartbinders. Resolve
        // their shared query through its receiver instead of five global scans.
        static OwnedFunctionCache have_item{STR("BP_TresHaveItem")};
        UFunction* const function_object = bp_have_item_path ==
                        STR("/Script/TresGame.TresGameBlueprintLibrary:BP_TresHaveItem")
                ? have_item.resolve(self_object) : nullptr;
        if (!function_object || !self_object || !world_context_object || runtime_token.empty())
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("missing prereq function={} self={} world={} token={}"),
                        function_object ? function_object->GetFullName() : std::wstring{STR("<null>")},
                        format_object_debug_name(self_object),
                        format_opaque_world_context_address(world_context_object),
                        runtime_token.empty() ? STR("<empty>") : std::wstring{runtime_token});
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        FProperty* const token_property = find_function_param_by_name(function_object, STR("InItemKeyName"));
        FProperty* const world_property = find_function_param_by_name(function_object, STR("WorldContextObject"));
        FProperty* const return_property = find_function_param_by_name(function_object, STR("ReturnValue"));
        if (!write_name_or_string_function_param(token_property, params.data(), runtime_token)
            || !world_property
            || !return_property
            || return_property->GetClass().GetName() != STR("BoolProperty"))
        {
            if (failure_reason)
            {
                *failure_reason = STR("failed BP_TresHaveItem parameter setup");
            }
            return false;
        }
        if (UObject** const world_value = world_property->ContainerPtrToValuePtr<UObject*>(params.data()))
        {
            *world_value = world_context_object;
        }
        else
        {
            if (failure_reason)
            {
                *failure_reason = STR("missing BP_TresHaveItem WorldContextObject storage");
            }
            return false;
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }
        has_item = static_cast<FBoolProperty*>(return_property)->GetPropertyValueInContainer(params.data());
        if (debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Calling BP_TresHaveItem token={} result={} | self={} | world={}\n"),
                    runtime_token,
                    has_item ? STR("true") : STR("false"),
                    format_object_debug_name(self_object),
                    format_opaque_world_context_address(world_context_object));
        }
        return true;
    }

    auto try_call_bp_tres_get_item_and_add_munny(
            std::wstring_view runtime_token,
            int32_t munny,
            UObject* world_context_object,
            bool show_info,
            RuntimeState& runtime_state,
            std::wstring_view bp_get_item_path,
            std::wstring_view bp_add_munny_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason) -> bool
    {
        UFunction* const add_munny_function = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr,
                nullptr,
                bp_add_munny_path);
        UObject* add_munny_self = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                runtime_state.object_cache(),
                blueprint_library_default_object_path);
        if (!add_munny_self)
        {
            add_munny_self = try_get_function_default_object(add_munny_function);
        }
        if (!add_munny_function || !add_munny_self || munny == 0)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("missing add-munny prereq function={} self={} amount={}"),
                        add_munny_function
                                ? add_munny_function->GetFullName()
                                : std::wstring{STR("<null>")},
                        format_object_debug_name(add_munny_self),
                        munny);
            }
            return false;
        }

        std::vector<std::byte> add_munny_params(
                static_cast<size_t>(add_munny_function->GetParmsSize()));
        std::fill(add_munny_params.begin(), add_munny_params.end(), std::byte{0});

        FProperty* munny_property = find_function_param_by_name(add_munny_function, STR("munny"));
        if (!munny_property)
        {
            munny_property = find_function_param_by_name(add_munny_function, STR("InMunny"));
        }
        if (!munny_property)
        {
            munny_property = find_first_function_param_by_class(
                    add_munny_function,
                    {STR("IntProperty")});
        }
        if (!write_integral_function_param(
                    munny_property,
                    add_munny_params.data(),
                    static_cast<uint64_t>(static_cast<int64_t>(munny))))
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("failed add-munny param setup property={} amount={}"),
                        munny_property
                                ? munny_property->GetName()
                                : std::wstring{STR("<null>")},
                        munny);
            }
            return false;
        }

        // Resolve and validate BP_AddMunny before the visible item grant. Once
        // inventory is credited, the remaining call is a void Blueprint event
        // with a fully prepared parameter buffer.
        const bool persistent_inventory =
                world_context_object && persistent_player_reward_context() == world_context_object;
        const bool item_granted = persistent_inventory
                ? try_call_persistent_inventory_grant(
                        runtime_token, world_context_object, show_info, failure_reason)
                : try_call_bp_tres_get_item(
                        runtime_token, world_context_object, show_info, runtime_state,
                        bp_get_item_path, blueprint_library_default_object_path, debug_logging,
                        failure_reason);
        if (!item_granted)
        {
            return false;
        }

        if (debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Calling BP_AddMunny amount={} | self={}\n"),
                    munny,
                    format_object_debug_name(add_munny_self));
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            add_munny_self->ProcessEvent(add_munny_function, add_munny_params.data());
        }
        return true;
    }

    auto try_call_bp_add_munny(
            int32_t munny,
            RuntimeState& runtime_state,
            std::wstring_view bp_add_munny_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason) -> bool
    {
        UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr,
                nullptr,
                bp_add_munny_path);
        UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                runtime_state.object_cache(),
                blueprint_library_default_object_path);
        if (!self_object)
        {
            self_object = try_get_function_default_object(function_object);
        }
        if (!function_object || !self_object || munny == 0)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("missing add-munny prereq function={} self={} amount={}"),
                        function_object
                                ? function_object->GetFullName()
                                : std::wstring{STR("<null>")},
                        format_object_debug_name(self_object),
                        munny);
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});

        FProperty* munny_property = find_function_param_by_name(function_object, STR("munny"));
        if (!munny_property)
        {
            munny_property = find_function_param_by_name(function_object, STR("InMunny"));
        }
        if (!munny_property)
        {
            munny_property = find_first_function_param_by_class(function_object, {STR("IntProperty")});
        }
        if (!write_integral_function_param(
                    munny_property,
                    params.data(),
                    static_cast<uint64_t>(static_cast<int64_t>(munny))))
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("failed add-munny param setup property={} amount={}"),
                        munny_property
                                ? munny_property->GetName()
                                : std::wstring{STR("<null>")},
                        munny);
            }
            return false;
        }

        if (debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Calling BP_AddMunny manual amount={} | self={}\n"),
                    munny,
                    format_object_debug_name(self_object));
        }
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }
        return true;
    }

    auto try_call_bp_tres_start_game_over(
            UObject* world_context_object,
            bool play_dead_se,
            RuntimeState& runtime_state,
            std::wstring_view bp_tres_start_game_over_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason) -> bool
    {
        // Recheck the actual pawn at execution time: a queued DeathLink may
        // have crossed a map transition since the worker last saw context.
        if (world_context_object && kh3ap::is_uobject_valid_fast(world_context_object) &&
            deathlink::is_station_of_awakening(std::wstring_view{world_context_object->GetFullName()}))
        {
            if (failure_reason) *failure_reason = STR("deathlink_disabled_station_of_awakening");
            return false;
        }
        const auto profile_started_at = std::chrono::steady_clock::now();
        UFunction* function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr,
                nullptr,
                bp_tres_start_game_over_path);
        if (function_object && !kh3ap::is_uobject_valid_fast(function_object))
        {
            function_object = nullptr;
        }
        const auto function_resolved_at = std::chrono::steady_clock::now();

        UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                runtime_state.object_cache(),
                blueprint_library_default_object_path);
        if (!self_object)
        {
            self_object = try_get_function_default_object(function_object);
        }
        const auto self_resolved_at = std::chrono::steady_clock::now();
        if (!function_object || !self_object || !world_context_object)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("missing prereq function={} self={} world={}"),
                        function_object ? function_object->GetFullName() : std::wstring{STR("<null>")},
                        format_object_debug_name(self_object),
                        format_object_debug_name(world_context_object));
            }
            return false;
        }
        if (is_game_viewport_context(world_context_object))
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("unsafe_viewport_context world={}"),
                        format_object_debug_name(world_context_object));
            }
            return false;
        }
        const auto context_validated_at = std::chrono::steady_clock::now();

        const size_t params_size =
                static_cast<size_t>(function_object->GetParmsSize());
        alignas(std::max_align_t)
                std::array<std::byte, s_game_over_inline_param_bytes> inline_params{};
        std::vector<std::byte> overflow_params{};
        std::byte* params = inline_params.data();
        if (params_size > inline_params.size())
        {
            overflow_params.assign(params_size, std::byte{0});
            params = overflow_params.data();
        }
        const auto params_allocated_at = std::chrono::steady_clock::now();

        FProperty* const world_context_property = find_function_param_by_name(
                function_object,
                STR("WorldContextObject"));
        FProperty* const game_over_kind_property = find_function_param_by_name(
                function_object,
                STR("InGameOverKind"));
        FProperty* const play_dead_se_property = find_function_param_by_name(
                function_object,
                STR("bPlayDeadSE"));
        const auto signature_resolved_at = std::chrono::steady_clock::now();

        if (FProperty* const world_property = world_context_property)
        {
            if (UObject** const world_value = world_property->ContainerPtrToValuePtr<UObject*>(params))
            {
                *world_value = world_context_object;
            }
        }
        if (game_over_kind_property)
        {
            if (uint8_t* const game_over_kind_value = game_over_kind_property->ContainerPtrToValuePtr<uint8_t>(params))
            {
                *game_over_kind_value = 0; // ETresGameOverKind::GAMEOVER_KIND_NORMAL
            }
        }
        if (play_dead_se_property)
        {
            if (bool* const play_dead_se_value = play_dead_se_property->ContainerPtrToValuePtr<bool>(params))
            {
                *play_dead_se_value = play_dead_se;
            }
        }
        const auto params_written_at = std::chrono::steady_clock::now();

        if (debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Calling BP_TresStartGameOver kind=GAMEOVER_KIND_NORMAL | self={} | world={} | play_dead_se={}\n"),
                    format_object_debug_name(self_object),
                    format_object_debug_name(world_context_object),
                    play_dead_se ? STR("true") : STR("false"));
        }

        std::chrono::steady_clock::time_point process_event_started_at{};
        std::chrono::steady_clock::time_point process_event_returned_at{};
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            process_event_started_at = std::chrono::steady_clock::now();
            self_object->ProcessEvent(function_object, params);
            process_event_returned_at = std::chrono::steady_clock::now();
        }
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] DeathLinkGameOverProfile function_us={} self_us={} validate_us={} allocate_us={} signature_us={} write_us={} dispatch_us={} wrapper_us={} total_us={} params_size={}\n"),
                elapsed_us(profile_started_at, function_resolved_at),
                elapsed_us(function_resolved_at, self_resolved_at),
                elapsed_us(self_resolved_at, context_validated_at),
                elapsed_us(context_validated_at, params_allocated_at),
                elapsed_us(params_allocated_at, signature_resolved_at),
                elapsed_us(signature_resolved_at, params_written_at),
                elapsed_us(process_event_started_at, process_event_returned_at),
                elapsed_us(profile_started_at, process_event_started_at),
                elapsed_us(profile_started_at, process_event_returned_at),
                params_size);
        return true;
    }

    auto try_call_get_game_flag(
            std::wstring_view flag_name,
            int32_t& value,
            RuntimeState& runtime_state,
            std::wstring_view get_game_flag_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason) -> bool
    {
        const RuntimeHotPathProfileScope profile{L"game_flags:integer_read"};
        // Cache only reflected layout, never the game flag value. A weak
        // owner invalidates its FProperty pointers after unload/map-transition GC.
        static thread_local GameFlagGetterLayout layout;
        UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                runtime_state.object_cache(),
                blueprint_library_default_object_path);
        UFunction* const function_object = layout.resolve(get_game_flag_path, self_object);
        if (!self_object)
        {
            self_object = try_get_function_default_object(function_object);
        }
        if (!function_object || !self_object || flag_name.empty())
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("missing prereq function={} self={} flag={}"),
                        function_object ? function_object->GetFullName() : std::wstring{STR("<null>")},
                        format_object_debug_name(self_object),
                        flag_name.empty() ? STR("<empty>") : std::wstring{flag_name});
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});

        FProperty* const flag_property = layout.flag;
        FProperty* const return_property = layout.result;
        if (!write_name_or_string_function_param(flag_property, params.data(), flag_name) ||
            !return_property)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("failed param setup flag_property={} return_property={}"),
                        flag_property ? flag_property->GetName() : std::wstring{STR("<null>")},
                        return_property ? return_property->GetName() : std::wstring{STR("<null>")});
            }
            return false;
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }

        int32_t* const return_value = return_property->ContainerPtrToValuePtr<int32_t>(params.data());
        if (!return_value)
        {
            if (failure_reason)
            {
                *failure_reason = STR("missing return value");
            }
            return false;
        }

        value = *return_value;
        if (debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Calling GetGameFlag flag={} value={} | self={}\n"),
                    flag_name,
                    value,
                    format_object_debug_name(self_object));
        }
        return true;
    }

    auto try_call_get_game_flag_boolean(
            std::wstring_view flag_name,
            bool& value,
            RuntimeState& runtime_state,
            std::wstring_view get_game_flag_boolean_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason) -> bool
    {
        const RuntimeHotPathProfileScope profile{L"game_flags:boolean_read"};
        // Cache only reflected layout, never the game flag value. A weak
        // owner invalidates its FProperty pointers after unload/map-transition GC.
        static thread_local GameFlagGetterLayout layout;
        UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                runtime_state.object_cache(),
                blueprint_library_default_object_path);
        UFunction* const function_object = layout.resolve(get_game_flag_boolean_path, self_object);
        if (!self_object)
        {
            self_object = try_get_function_default_object(function_object);
        }
        if (!function_object || !self_object || flag_name.empty())
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("missing prereq function={} self={} flag={}"),
                        function_object ? function_object->GetFullName() : std::wstring{STR("<null>")},
                        format_object_debug_name(self_object),
                        flag_name.empty() ? STR("<empty>") : std::wstring{flag_name});
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});

        FProperty* const flag_property = layout.flag;
        FProperty* const return_property = layout.result;
        if (!write_name_or_string_function_param(flag_property, params.data(), flag_name) ||
            !return_property || return_property->GetClass().GetName() != STR("BoolProperty"))
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("failed param setup flag_property={} return_property={}"),
                        flag_property ? flag_property->GetName() : std::wstring{STR("<null>")},
                        return_property ? return_property->GetName() : std::wstring{STR("<null>")});
            }
            return false;
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }

        value = static_cast<FBoolProperty*>(return_property)
                        ->GetPropertyValueInContainer(params.data());
        if (debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Calling GetGameFlagBoolean flag={} value={} | self={}\n"),
                    flag_name,
                    value ? STR("true") : STR("false"),
                    format_object_debug_name(self_object));
        }
        return true;
    }

    auto try_call_set_game_flag_boolean(
            std::wstring_view flag_name,
            bool value,
            RuntimeState& runtime_state,
            std::wstring_view set_game_flag_boolean_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason) -> bool
    {
        UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr,
                nullptr,
                set_game_flag_boolean_path);
        UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                runtime_state.object_cache(),
                blueprint_library_default_object_path);
        if (!self_object)
        {
            self_object = try_get_function_default_object(function_object);
        }
        if (!function_object || !self_object || flag_name.empty())
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("missing prereq function={} self={} flag={}"),
                        function_object ? function_object->GetFullName() : std::wstring{STR("<null>")},
                        format_object_debug_name(self_object),
                        flag_name.empty() ? STR("<empty>") : std::wstring{flag_name});
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});

        FProperty* flag_property = find_function_param_by_name(function_object, STR("InFlagName"));
        if (!flag_property)
        {
            flag_property = find_first_function_param_by_class(function_object, {STR("NameProperty")});
        }

        FProperty* value_property = find_function_param_by_name(function_object, STR("bValue"));
        if (!value_property)
        {
            value_property = find_function_param_by_name(function_object, STR("Value"));
        }
        if (!value_property)
        {
            value_property = find_function_param_by_name(function_object, STR("InValue"));
        }
        if (!value_property)
        {
            value_property = find_first_function_param_by_class(function_object, {STR("BoolProperty")});
        }

        const bool wrote_flag = write_name_or_string_function_param(flag_property, params.data(), flag_name);
        const bool wrote_value = write_bool_function_param(value_property, params.data(), value);
        if (!wrote_flag || !wrote_value)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("failed param write flag_property={} value_property={} wrote_flag={} wrote_value={}"),
                        flag_property ? flag_property->GetName() : std::wstring{STR("<null>")},
                        value_property ? value_property->GetName() : std::wstring{STR("<null>")},
                        wrote_flag ? STR("true") : STR("false"),
                        wrote_value ? STR("true") : STR("false"));
            }
            return false;
        }

        if (debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Calling SetGameFlagBoolean flag={} value={} | self={}\n"),
                    flag_name,
                    value ? STR("true") : STR("false"),
                    format_object_debug_name(self_object));
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }

        return true;
    }

    auto try_call_set_game_flag(
            std::wstring_view flag_name,
            int32_t value,
            RuntimeState& runtime_state,
            std::wstring_view set_game_flag_path,
            std::wstring_view blueprint_library_default_object_path,
            bool debug_logging,
            std::wstring* failure_reason) -> bool
    {
        UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr,
                nullptr,
                set_game_flag_path);
        UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                runtime_state.object_cache(),
                blueprint_library_default_object_path);
        if (!self_object)
        {
            self_object = try_get_function_default_object(function_object);
        }
        if (!function_object || !self_object || flag_name.empty())
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("missing prereq function={} self={} flag={}"),
                        function_object ? function_object->GetFullName() : std::wstring{STR("<null>")},
                        format_object_debug_name(self_object),
                        flag_name.empty() ? STR("<empty>") : std::wstring{flag_name});
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});

        FProperty* flag_property = find_function_param_by_name(function_object, STR("InFlagName"));
        if (!flag_property)
        {
            flag_property = find_first_function_param_by_class(function_object, {STR("NameProperty")});
        }

        FProperty* value_property = find_function_param_by_name(function_object, STR("InValue"));
        if (!value_property)
        {
            value_property = find_function_param_by_name(function_object, STR("Value"));
        }
        if (!value_property)
        {
            value_property = find_first_function_param_by_class(function_object, {STR("IntProperty")});
        }

        const bool wrote_flag = write_name_or_string_function_param(flag_property, params.data(), flag_name);
        const bool wrote_value = write_integral_function_param(
                value_property,
                params.data(),
                static_cast<uint64_t>(static_cast<int64_t>(value)));
        if (!wrote_flag || !wrote_value)
        {
            if (failure_reason)
            {
                *failure_reason = std::format(
                        STR("failed param write flag_property={} value_property={} wrote_flag={} wrote_value={}"),
                        flag_property ? flag_property->GetName() : std::wstring{STR("<null>")},
                        value_property ? value_property->GetName() : std::wstring{STR("<null>")},
                        wrote_flag ? STR("true") : STR("false"),
                        wrote_value ? STR("true") : STR("false"));
            }
            return false;
        }

        if (debug_logging)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Calling SetGameFlag flag={} value={} | self={}\n"),
                    flag_name,
                    value,
                    format_object_debug_name(self_object));
        }

        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
        }

        return true;
    }
}
