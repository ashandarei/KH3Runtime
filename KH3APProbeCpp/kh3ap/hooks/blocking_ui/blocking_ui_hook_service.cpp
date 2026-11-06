#include <chrono>
#include "kh3ap/hooks/blocking_ui/blocking_ui_hook_service.h"

#include "kh3ap/hooks/blocking_ui/blocking_ui_focus_service.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <unordered_set>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto render_focus_rule_never = 3;
        constexpr auto max_observed_class_name_offset = 0x100;
        // FName is two uint32 values in this UE4 build. Comparing the raw bits
        // avoids string allocation and ABI-fragile FName operator imports in the
        // ProcessEvent hot path.
        [[nodiscard]] auto fname_bits(const FName& name) -> uint64_t
        {
            static_assert(sizeof(FName) == sizeof(uint64_t));
            uint64_t bits{};
            std::memcpy(&bits, &name, sizeof(bits));
            return bits;
        }

        [[nodiscard]] auto fname_is_none(const FName& name) -> bool
        {
            return fname_bits(name) == 0;
        }

        [[nodiscard]] auto fname_equals(const FName& left, const FName& right) -> bool
        {
            return fname_bits(left) == fname_bits(right);
        }

        using GetNamePrivateFunction = const FName& (*)(const UObjectBase*);

        [[nodiscard]] auto resolve_get_name_private() -> GetNamePrivateFunction
        {
            const HMODULE ue4ss_module = GetModuleHandleW(L"UE4SS.dll");
            if (!ue4ss_module)
            {
                return nullptr;
            }

            // RE-UE4SS has shipped headers spelling FName as either a class or
            // a struct. The object layout is the same, but MSVC decorates the
            // return type differently, so accept both exports.
            constexpr const char* candidate_exports[]{
                "?GetNamePrivate@UObjectBase@Unreal@RC@@QEBAAEBVFName@23@XZ",
                "?GetNamePrivate@UObjectBase@Unreal@RC@@QEBAAEBUFName@23@XZ",
            };
            for (const char* const export_name : candidate_exports)
            {
                if (const FARPROC address = GetProcAddress(ue4ss_module, export_name))
                {
                    static_assert(sizeof(address) == sizeof(GetNamePrivateFunction));
                    GetNamePrivateFunction function{};
                    std::memcpy(&function, &address, sizeof(function));
                    return function;
                }
            }
            return nullptr;
        }

        [[nodiscard]] auto get_frame_current_native_function(FFrame& frame) -> UFunction*
        {
            // KH3 ships UE4.17. Read the UE4.17 FFrame layout directly instead
            // of importing FFrame::CurrentNativeFunction(). GoA's UE4SS build
            // returns UFunction*& while newer headers declare UFunction*, and
            // that decorated-export mismatch prevents the entire mod DLL from
            // loading. This is the same version-specific layout access already
            // used by get_frame_node().
            auto* const frame_impl = reinterpret_cast<FFrame_50_AndBelow*>(&frame);
            return frame_impl->CurrentNativeFunction;
        }

        [[nodiscard]] auto read_bool_param(UFunction* function, void* params,
                                           std::wstring_view param_name) -> std::optional<bool>
        {
            if (!function || !params)
            {
                return std::nullopt;
            }

            FProperty* const property = find_function_param_by_name(function, param_name);
            if (!property)
            {
                return std::nullopt;
            }
            void* const value = property->ContainerPtrToValuePtr<void>(params);
            if (!value)
            {
                return std::nullopt;
            }
            return read_unsigned_param_value(value, property->GetElementSize()) != 0;
        }

        enum class UiLifecycleSignal : uint8_t
        {
            None,
            FocusIn,
            FocusOut,
            Open,
            Close,
            Show,
            Hide,
        };
    } // namespace

    struct RuntimeBlockingUiHookService::Impl
    {
        explicit Impl(RuntimeBlockingUiHookConfig value)
            : config(std::move(value)), focus_service(config)
        {
        }

        ~Impl()
        {
            uninstall(STR("service_destructor"));
        }

        [[nodiscard]] auto object_name(const UObjectBase* object) const -> const FName*
        {
            if (!object || object_name_offset == 0)
            {
                return nullptr;
            }
            const auto* const bytes = reinterpret_cast<const std::byte*>(object);
            return reinterpret_cast<const FName*>(bytes + object_name_offset);
        }

        [[nodiscard]] auto function_owner_is(UFunction* function,
                                             const FName& expected_owner_name) const -> bool
        {
            const UObject* const owner = function ? function->GetOuterPrivate() : nullptr;
            const FName* const owner_name = object_name(owner);
            return owner_name && fname_equals(*owner_name, expected_owner_name);
        }

        [[nodiscard]] auto ui_focus_context_name(UObject* context) const
            -> std::optional<std::wstring>
        {
            const UClass* const context_class = context ? context->GetClassPrivate() : nullptr;
            if (!context_class)
            {
                return std::nullopt;
            }
            std::wstring class_name = context_class->GetName();
            return class_name.empty() ? std::nullopt
                                      : std::optional<std::wstring>{std::move(class_name)};
        }

        auto on_process_event_post(UObject* context, UFunction* function, void* params) -> void
        {
            const FName* const function_name = object_name(function);
            if (!function_name)
            {
                return;
            }

            // ProcessEvent carries the observed KH3 UI focus callbacks and the
            // two engine pause fallbacks. The optional script routes below
            // cover Blueprint dispatch configurations that enable them.
            const bool is_set_game_paused = fname_equals(*function_name, set_game_paused_name);
            const bool is_set_pause = fname_equals(*function_name, set_pause_name);
            if (!is_set_game_paused && !is_set_pause)
            {
                const UiLifecycleSignal signal = match_ui_lifecycle_signal(function);
                if (signal != UiLifecycleSignal::None)
                {
                    publish_ui_lifecycle_signal(context, signal);
                }
                return;
            }

            const FName& expected_owner =
                is_set_game_paused ? gameplay_statics_class_name : player_controller_class_name;
            if (!function_owner_is(function, expected_owner))
            {
                return;
            }

            const std::wstring_view param_name =
                is_set_game_paused ? STR("bPaused") : STR("bPause");
            const auto pause_requested = read_bool_param(function, params, param_name);
            if (!pause_requested.has_value())
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] RuntimeBlockingUI ignored event={} "
                        "reason=missing_pause_parameter parameter={}\n"),
                    is_set_game_paused ? STR("SetGamePaused") : STR("SetPause"), param_name);
                return;
            }

            focus_service.observe_engine_pause(
                *pause_requested,
                is_set_game_paused ? STR("/Script/Engine.GameplayStatics:SetGamePaused")
                                   : STR("/Script/Engine.PlayerController:SetPause"),
                is_set_game_paused ? STR("SetGamePaused") : STR("SetPause"));
        }

        [[nodiscard]] auto match_ui_lifecycle_signal(UFunction* function) const -> UiLifecycleSignal
        {
            const FName* const function_name = object_name(function);
            if (!function_name)
            {
                return UiLifecycleSignal::None;
            }
            if (fname_equals(*function_name, on_focus_in_name))
            {
                return UiLifecycleSignal::FocusIn;
            }
            if (fname_equals(*function_name, on_focus_out_name))
            {
                return UiLifecycleSignal::FocusOut;
            }
            if (fname_equals(*function_name, on_open_name))
            {
                return UiLifecycleSignal::Open;
            }
            if (fname_equals(*function_name, on_close_name))
            {
                return UiLifecycleSignal::Close;
            }
            if (fname_equals(*function_name, on_show_name))
            {
                return UiLifecycleSignal::Show;
            }
            if (fname_equals(*function_name, on_hide_name))
            {
                return UiLifecycleSignal::Hide;
            }
            return UiLifecycleSignal::None;
        }

        [[nodiscard]] static auto ui_lifecycle_event_name(UiLifecycleSignal signal)
            -> std::wstring_view
        {
            switch (signal)
            {
            case UiLifecycleSignal::FocusIn:
                return STR("OnFocusIn");
            case UiLifecycleSignal::FocusOut:
                return STR("OnFocusOut");
            case UiLifecycleSignal::Open:
                return STR("OnOpen");
            case UiLifecycleSignal::Close:
                return STR("OnClose");
            case UiLifecycleSignal::Show:
                return STR("OnShow");
            case UiLifecycleSignal::Hide:
                return STR("OnHide");
            case UiLifecycleSignal::None:
            default:
                return STR("<none>");
            }
        }

        auto publish_ui_lifecycle_signal(UObject* context, UiLifecycleSignal signal) -> void
        {
            // A derived UI class can override the base lifecycle UFunction, so
            // the function owner is not stable. Filter the runtime context by
            // KH3's native TresUI class prefix. This includes the pause root
            // and Gummiphone screens without enumerating live objects.
            const auto context_name = ui_focus_context_name(context);
            if (!context_name.has_value() || context_name->rfind(STR("TresUI"), 0) != 0)
            {
                return;
            }

            const bool focus_in = signal == UiLifecycleSignal::FocusIn ||
                                  signal == UiLifecycleSignal::Open ||
                                  signal == UiLifecycleSignal::Show;
            focus_service.observe_ui_lifecycle(reinterpret_cast<uintptr_t>(context), *context_name,
                                               ui_lifecycle_event_name(signal), focus_in,
                                               std::chrono::steady_clock::now());
        }

        // Slate's dotted focus brush is independent of the thumbnail material.
        // Hide only that brush while the GoA popup owns focus; retain all actual
        // keyboard/controller focus and restore the game's policy on close/travel.
        static auto focus_rule_value(UObject* settings) -> uint8_t*
        {
            if (!is_uobject_valid_fast(settings))
            {
                return nullptr;
            }
            auto* property = settings->GetPropertyByNameInChain(STR("RenderFocusRule"));
            return property && property->GetElementSize() == 1
                       ? property->ContainerPtrToValuePtr<uint8_t>(settings)
                       : nullptr;
        }

        auto restore_selector_focus_rule() -> void
        {
            if (!original_focus_rule.has_value())
            {
                return;
            }
            // Resolve the CDO anew. The pinned UE4SS weak-pointer wrapper does
            // not allocate missing serials, which some UI/settings objects lack.
            auto* settings = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/Engine.Default__UserInterfaceSettings"));
            auto* value = focus_rule_value(settings);
            if (value && *value == render_focus_rule_never) // ERenderFocusRule::Never in UE4.17.
            {
                *value = *original_focus_rule;
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] GoASelectorFocusOutline restored rule={} readback={}\n"),
                    *original_focus_rule, *value);
            }
            original_focus_rule.reset();
            selector_owners.clear();
        }

        auto observe_selector_lifecycle(UObject* context, UFunction* function) -> void
        {
            const FName* const name = object_name(function);
            if (!name ||
                (!fname_equals(*name, construct_name) && !fname_equals(*name, destruct_name)))
            {
                return;
            }
            if (!is_uobject_valid_fast(context))
            {
                return;
            }
            auto* widget_class = context->GetClassPrivate();
            if (!is_uobject_valid_fast(widget_class))
            {
                return;
            }
            const FName* const class_name = object_name(widget_class);
            if (!class_name || !fname_equals(*class_name, selector_class_name))
            {
                return;
            }
            if (fname_equals(*name, destruct_name))
            {
                selector_owners.erase(reinterpret_cast<uintptr_t>(context));
                if (selector_owners.empty())
                {
                    restore_selector_focus_rule();
                }
                return;
            }
            const auto identity = reinterpret_cast<uintptr_t>(context);
            if (selector_owners.contains(identity))
            {
                return;
            }
            if (!original_focus_rule.has_value())
            {
                auto* settings = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Script/Engine.Default__UserInterfaceSettings"));
                auto* value = focus_rule_value(settings);
                if (!value || *value > render_focus_rule_never)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] GoASelectorFocusOutline unavailable "
                            "reason=focus_rule_unresolved\n"));
                    return;
                }
                original_focus_rule = *value;
                *value = render_focus_rule_never;
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] GoASelectorFocusOutline suppressed original_rule={} "
                        "readback={} keyboard_focus=preserved\n"),
                    *original_focus_rule, *value);
            }
            // Identities are never dereferenced later; native Destruct and the
            // verified map-load event retire ownership without retaining UObjects.
            selector_owners.insert(identity);
        }

        auto process_pending_focus_release(std::chrono::steady_clock::time_point now) -> void
        {
            focus_service.process_pending_release(now);
        }

        auto observe_pre_load_map(UFunction* function) -> void
        {
            const FName* const name = object_name(function);
            if (!name || !fname_equals(*name, on_pre_load_map_name) || !config.before_map_load ||
                !is_uobject_valid_fast(function))
            {
                return;
            }
            // Only allocate/inspect ownership after the exact raw-FName match.
            // These Blueprint events cannot use RegisterHook before KH3's
            // script dispatcher is installed; ProcessEvent covers native calls.
            const auto path = function->GetFullName();
            if (path != STR("Function /Script/TresGame.TresNotifyEventToBlueprint:OnPreLoadMap") &&
                path != STR("Function "
                            "/Game/System/"
                            "BP_NotifyEventToBlueprint.BP_NotifyEventToBlueprint_C:OnPreLoadMap"))
            {
                return;
            }
            restore_selector_focus_rule();
            config.before_map_load();
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Portal lifecycle pre-load event={}\n"), path);
        }

        static auto process_event_pre_callback(UObject* context, UFunction* function,
                                               void* /*params*/) -> void
        {
            Impl* const instance = active_instance.load(std::memory_order_acquire);
            if (!instance)
            {
                return;
            }
            try
            {
                instance->observe_selector_lifecycle(context, function);
                instance->observe_pre_load_map(function);
            }
            catch (...)
            {
            }
        }

        auto on_script_dispatch_pre(UObject* context, FFrame& stack) -> void
        {
            UFunction* const frame_function = get_frame_node(stack);
            UFunction* const native_function = get_frame_current_native_function(stack);
            observe_selector_lifecycle(context, frame_function);
            observe_pre_load_map(frame_function);
            if (native_function != frame_function)
            {
                observe_selector_lifecycle(context, native_function);
                observe_pre_load_map(native_function);
            }
            UiLifecycleSignal signal = match_ui_lifecycle_signal(frame_function);
            if (signal == UiLifecycleSignal::None && native_function != frame_function)
            {
                signal = match_ui_lifecycle_signal(native_function);
            }

            // ProcessInternal is a very hot Blueprint path. Unrelated calls
            // perform only raw FName comparisons and allocate nothing.
            if (signal == UiLifecycleSignal::None)
            {
                return;
            }
            publish_ui_lifecycle_signal(context, signal);
        }

        static auto process_event_post_callback(UObject* context, UFunction* function, void* params)
            -> void
        {
            Impl* const instance = active_instance.load(std::memory_order_acquire);
            if (!instance)
            {
                return;
            }
            try
            {
                instance->on_process_event_post(context, function, params);
            }
            catch (...)
            {
            }
        }

        static auto script_dispatch_pre_callback(UObject* context, FFrame& stack, void* /*result*/)
            -> void
        {
            Impl* const instance = active_instance.load(std::memory_order_acquire);
            if (!instance)
            {
                return;
            }
            try
            {
                instance->on_script_dispatch_pre(context, stack);
            }
            catch (...)
            {
            }
        }

        auto install_once() -> void
        {
            if (!config.enabled || installed)
            {
                return;
            }

            Output::send<LogLevel::Verbose>(STR(
                "[KH3APProbeCpp] RuntimeBlockingUI observer install stage=unreal_init begin\n"));

            const GetNamePrivateFunction get_name_private = resolve_get_name_private();
            const UClass* const class_class = UClass::StaticClass();
            if (!get_name_private || !class_class)
            {
                Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] RuntimeBlockingUI observer install failed "
                        "reason=get_name_private_unresolved\n"));
                return;
            }

            const uintptr_t class_address = reinterpret_cast<uintptr_t>(class_class);
            const uintptr_t name_address =
                reinterpret_cast<uintptr_t>(std::addressof(get_name_private(class_class)));
            if (name_address <= class_address ||
                name_address - class_address > max_observed_class_name_offset)
            {
                Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] RuntimeBlockingUI observer install failed "
                        "reason=get_name_private_offset_invalid offset={}\n"),
                    name_address - class_address);
                return;
            }
            object_name_offset = static_cast<size_t>(name_address - class_address);

            on_focus_in_name = FName{STR("OnFocusIn"), FNAME_Add};
            on_focus_out_name = FName{STR("OnFocusOut"), FNAME_Add};
            on_open_name = FName{STR("OnOpen"), FNAME_Add};
            on_close_name = FName{STR("OnClose"), FNAME_Add};
            on_show_name = FName{STR("OnShow"), FNAME_Add};
            on_hide_name = FName{STR("OnHide"), FNAME_Add};
            set_game_paused_name = FName{STR("SetGamePaused"), FNAME_Add};
            set_pause_name = FName{STR("SetPause"), FNAME_Add};
            gameplay_statics_class_name = FName{STR("GameplayStatics"), FNAME_Add};
            player_controller_class_name = FName{STR("PlayerController"), FNAME_Add};
            on_pre_load_map_name = FName{STR("OnPreLoadMap"), FNAME_Add};
            construct_name = FName{STR("Construct"), FNAME_Add};
            destruct_name = FName{STR("Destruct"), FNAME_Add};
            selector_class_name = FName{STR("W_SaveChoiceUI_C"), FNAME_Add};

            if (fname_is_none(on_focus_in_name) || fname_is_none(on_focus_out_name) ||
                fname_is_none(on_open_name) || fname_is_none(on_close_name) ||
                fname_is_none(on_show_name) || fname_is_none(on_hide_name) ||
                fname_is_none(set_game_paused_name) || fname_is_none(set_pause_name) ||
                fname_is_none(gameplay_statics_class_name) ||
                fname_is_none(player_controller_class_name) || fname_is_none(on_pre_load_map_name))
            {
                Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] RuntimeBlockingUI observer "
                                                  "install failed reason=fname_unresolved\n"));
                return;
            }

            // UE4SS leaves both script dispatch detours off by default in the
            // KH3 configuration. Install the supported UE4.17 ProcessInternal
            // path and the resolved ProcessLocalScriptFunction path so native
            // and derived Blueprint UI focus implementations are both covered.
            if (!Hook::StaticStorage::ProcessInternalDetour.get())
            {
                try
                {
                    HookProcessInternal();
                }
                catch (...)
                {
                    Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] RuntimeBlockingUI observer install failed "
                            "reason=process_internal_hook_exception\n"));
                    return;
                }
            }
            const bool process_internal_available =
                Hook::StaticStorage::ProcessInternalDetour.get() != nullptr;
            if (!process_internal_available)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] RuntimeBlockingUI optional "
                                                    "route unavailable route=process_internal\n"));
            }
            if (!Hook::StaticStorage::ProcessLocalScriptFunctionDetour.get())
            {
                try
                {
                    HookProcessLocalScriptFunction();
                }
                catch (...)
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] RuntimeBlockingUI observer optional "
                            "ProcessLocalScriptFunction hook failed\n"));
                }
            }

            active_instance.store(this, std::memory_order_release);
            if (process_internal_available)
            {
                bool process_internal_expected = false;
                if (process_internal_callback_registered.compare_exchange_strong(
                        process_internal_expected, true, std::memory_order_acq_rel))
                {
                    try
                    {
                        Hook::RegisterProcessInternalPreCallback(
                            &Impl::script_dispatch_pre_callback);
                    }
                    catch (...)
                    {
                        process_internal_callback_registered.store(false,
                                                                   std::memory_order_release);
                        Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] RuntimeBlockingUI optional route registration "
                                "failed route=process_internal\n"));
                    }
                }
            }

            if (Hook::StaticStorage::ProcessLocalScriptFunctionDetour.get())
            {
                bool process_local_script_expected = false;
                if (process_local_script_callback_registered.compare_exchange_strong(
                        process_local_script_expected, true, std::memory_order_acq_rel))
                {
                    try
                    {
                        Hook::RegisterProcessLocalScriptFunctionPreCallback(
                            &Impl::script_dispatch_pre_callback);
                    }
                    catch (...)
                    {
                        process_local_script_callback_registered.store(false,
                                                                       std::memory_order_release);
                        Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] RuntimeBlockingUI observer optional "
                                "ProcessLocalScriptFunction callback failed\n"));
                    }
                }
            }

            bool expected = false;
            if (process_event_callback_registered.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel))
            {
                try
                {
                    Hook::RegisterProcessEventPostCallback(&Impl::process_event_post_callback);
                    Hook::RegisterProcessEventPreCallback(&Impl::process_event_pre_callback);
                }
                catch (...)
                {
                    process_event_callback_registered.store(false, std::memory_order_release);
                    Impl* active = this;
                    active_instance.compare_exchange_strong(active, nullptr,
                                                            std::memory_order_acq_rel);
                    Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] RuntimeBlockingUI observer install failed "
                            "reason=process_event_registration_exception\n"));
                    return;
                }
            }

            installed = true;
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimeBlockingUI observer installed "
                    "mode=independent_script_pre+process_event_post script_lifecycle_names=6 "
                    "process_event_lifecycle_names=6 process_event_pause_names=2 "
                    "process_internal_available={} process_local_script_available={} "
                    "frame_and_native_function=ue4_17_layout context_prefix_filter=TresUI "
                    "focus_ownership=identity_set handoff_grace_ms={} pending_fast_path=atomic "
                    "generation_guard=true object_scans=false game_state_polling=false "
                    "isreal_checks=false\n"),
                process_internal_available,
                Hook::StaticStorage::ProcessLocalScriptFunctionDetour.get() != nullptr,
                static_cast<uint64_t>(config.focus_handoff_grace.count()));
        }

        auto uninstall(std::wstring_view reason) -> void
        {
            restore_selector_focus_rule();
            if (!installed)
            {
                return;
            }

            Impl* active = this;
            active_instance.compare_exchange_strong(active, nullptr, std::memory_order_acq_rel);
            focus_service.reset();
            installed = false;
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimeBlockingUI observer deactivated reason={}\n"), reason);
        }

        RuntimeBlockingUiHookConfig config;
        RuntimeBlockingUiFocusService focus_service;
        size_t object_name_offset{};
        FName on_focus_in_name{};
        FName on_focus_out_name{};
        FName on_open_name{};
        FName on_close_name{};
        FName on_show_name{};
        FName on_hide_name{};
        FName set_game_paused_name{};
        FName set_pause_name{};
        FName gameplay_statics_class_name{};
        FName player_controller_class_name{};
        FName on_pre_load_map_name{};
        FName construct_name{};
        FName destruct_name{};
        FName selector_class_name{};
        std::unordered_set<uintptr_t> selector_owners;
        std::optional<uint8_t> original_focus_rule;
        bool installed{false};

        inline static std::atomic<Impl*> active_instance{nullptr};
        inline static std::atomic<bool> process_internal_callback_registered{false};
        inline static std::atomic<bool> process_local_script_callback_registered{false};
        inline static std::atomic<bool> process_event_callback_registered{false};
    };

    RuntimeBlockingUiHookService::RuntimeBlockingUiHookService(RuntimeBlockingUiHookConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeBlockingUiHookService::~RuntimeBlockingUiHookService() = default;
    RuntimeBlockingUiHookService::RuntimeBlockingUiHookService(
        RuntimeBlockingUiHookService&&) noexcept = default;
    auto RuntimeBlockingUiHookService::operator=(RuntimeBlockingUiHookService&&) noexcept
        -> RuntimeBlockingUiHookService& = default;

    auto RuntimeBlockingUiHookService::install_once() -> void
    {
        impl_->install_once();
    }

    auto RuntimeBlockingUiHookService::process_pending_focus_release(
        std::chrono::steady_clock::time_point now) -> void
    {
        impl_->process_pending_focus_release(now);
    }

    auto RuntimeBlockingUiHookService::uninstall(std::wstring_view reason) -> void
    {
        impl_->uninstall(reason);
    }
} // namespace kh3ap
