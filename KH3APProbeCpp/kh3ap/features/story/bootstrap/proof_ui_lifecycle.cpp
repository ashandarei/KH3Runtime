#include "kh3ap/features/story/bootstrap/proof_ui_lifecycle.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap_calls.h"
#include "kh3ap/core/reflection/owned_function_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>
#include <atomic>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace bootstrap_native;
    namespace
    {
        auto live_weak_object(const FWeakObjectPtr& weak) -> UObject*
        {
            auto* object = weak.Get();
            return is_uobject_valid_fast(object) ? object : nullptr;
        }
        auto widget_in_viewport(UObject* widget) -> std::optional<bool>
        {
            static OwnedFunctionCache getter{L"IsInViewport"};
            auto* fn = getter.resolve(widget);
            auto* result = fn ? find_function_param_by_name(fn, L"ReturnValue") : nullptr;
            if (!result || !result->IsA<FBoolProperty>() || result->GetOffset_Internal() != 0 ||
                fn->GetParmsSize() != 1)
            {
                return {};
            }
            uint8_t params{};
            widget->ProcessEvent(fn, &params);
            return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(&params);
        }
        auto widget_remove_function(UObject* widget) -> UFunction*
        {
            static OwnedFunctionCache remove{L"RemoveFromParent"};
            auto* fn = remove.resolve(widget);
            return fn && (fn->GetFunctionFlags() & FUNC_Native) && fn->GetParmsSize() == 0
                       ? fn
                       : nullptr;
        }
    } // namespace
    struct ProofUiLifecycle::Impl : FUObjectDeleteListener
    {
        explicit Impl(std::chrono::milliseconds interval) : retry_interval(interval)
        {
        }
        static constexpr auto remove_widget_path = L"/Script/UMG.Widget:RemoveFromParent";
        ~Impl() override
        {
            watched_widget.store(0);
            if (remove_hook_installed)
            {
                try
                {
                    UObjectGlobals::UnregisterHook(remove_widget_path, remove_hook_ids);
                }
                catch (...)
                {
                }
            }
            if (delete_listener_installed)
            {
                UObjectArray::RemoveUObjectDeleteListener(this);
            }
        }
        auto NotifyUObjectDeleted(const UObjectBase* object, int32) -> void override
        {
            // The object can already be invalid and this callback can run on
            // a GC thread. Compare address bits only; keep ownership weak.
            if (object && reinterpret_cast<uintptr_t>(object) ==
                              watched_widget.load(std::memory_order_acquire))
            {
                widget_deleted.store(true, std::memory_order_release);
            }
        }
        auto OnUObjectArrayShutdown() -> void override
        {
            watched_widget.store(0);
            if (delete_listener_installed)
            {
                UObjectArray::RemoveUObjectDeleteListener(this);
            }
            delete_listener_installed = false;
            remove_hook_installed = false;
        }
        auto install_lifecycle_events() -> bool
        {
            if (remove_hook_installed && delete_listener_installed)
            {
                return true;
            }
            try
            {
                if (!remove_hook_installed)
                {
                    auto* fn = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                                                                            remove_widget_path);
                    if (!is_uobject_valid_fast(fn) || !(fn->GetFunctionFlags() & FUNC_Native) ||
                        fn->GetParmsSize() != 0)
                    {
                        throw std::runtime_error("RemoveFromParent_layout");
                    }
                    remove_hook_ids = UObjectGlobals::RegisterHook(
                        fn,
                        [](UnrealScriptFunctionCallableContext&, void*)
                        {
                        },
                        [this](UnrealScriptFunctionCallableContext& event, void*)
                        {
                            if (event.Context && reinterpret_cast<uintptr_t>(event.Context) ==
                                                     watched_widget.load(std::memory_order_acquire))
                            {
                                widget_removed.store(true, std::memory_order_release);
                            }
                        },
                        nullptr);
                    remove_hook_installed = true;
                }
                if (!delete_listener_installed)
                {
                    UObjectArray::AddUObjectDeleteListener(this);
                    delete_listener_installed = true;
                }
                return true;
            }
            catch (const std::exception& error)
            {
                const std::string message = error.what();
                report_failure(L"lifecycle_events", lifecycle_failure,
                               std::wstring(message.begin(), message.end()));
                return false;
            }
        }

        auto set_battle_level_map_visible(bool visible) -> bool
        {
            auto* widget = live_weak_object(proof_ui);
            if (!widget)
            {
                return !visible;
            }
            auto* container =
                try_read_reflected_object_property_value(widget, STR("battle_level_backing"));
            if (!is_uobject_valid_fast(container))
            {
                return !visible;
            }
            auto* function = container->GetFunctionByNameInChain(STR("SetVisibility"));
            if (!is_uobject_valid_fast(function))
            {
                return false;
            }
            auto* input = find_function_param_by_name(function, STR("InVisibility"));
            auto* visibility = container->GetPropertyByNameInChain(STR("Visibility"));
            if (!input || input->GetElementSize() != 1 || !visibility ||
                visibility->GetElementSize() != 1)
            {
                return false;
            }
            // ESlateVisibility: Collapsed=1, SelfHitTestInvisible=4.
            const uint8_t value = visible ? 4 : 1;
            std::vector<std::byte> params(function->GetParmsSize());
            if (!write_integral_function_param(input, params.data(), value))
            {
                return false;
            }
            container->ProcessEvent(function, params.data());
            if (read_unsigned_param_value(visibility->ContainerPtrToValuePtr<void>(container), 1) !=
                value)
            {
                return false;
            }
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Battle level map display visible={} widget={}\n"), visible,
                container->GetPathName());
            return true;
        }

        auto retire_proof_ui() -> void
        {
            watched_widget.store(0);
            if (auto* widget = live_weak_object(proof_ui))
            {
                if (auto* remove = widget_remove_function(widget))
                {
                    widget->ProcessEvent(remove, nullptr);
                }
            }
            proof_ui = nullptr;
        }

        auto refresh_viewport() -> bool
        {
            if (viewport_check_pending)
            {
                auto* widget = live_weak_object(proof_ui);
                const auto in_view = widget ? widget_in_viewport(widget) : std::optional{false};
                if (!in_view)
                {
                    return false;
                }
                viewport_check_pending = false;
                if (!*in_view)
                {
                    added = false;
                    proof_ui = nullptr;
                    watched_widget.store(0);
                }
            }

            return true;
        }
        auto ensure_widget(UObject* pawn, UClass* widget_class,
                           std::chrono::steady_clock::time_point now) -> bool
        {
            std::wstring failure;
            if (!added && widget_class &&
                (next_proof_ui_create_attempt_at == std::chrono::steady_clock::time_point{} ||
                 now >= next_proof_ui_create_attempt_at))
            {
                next_proof_ui_create_attempt_at = now + retry_interval;
                failure.clear();
                UObject* widget = nullptr;
                if (create_and_add_proof_ui(pawn, widget_class, &widget, &failure))
                {
                    proof_ui = make_uobject_weak(widget);
                    if (live_weak_object(proof_ui) != widget)
                    {
                        // Never leave an untracked viewport widget behind if
                        // the platform cannot allocate its weak identity.
                        if (auto* remove = widget_remove_function(widget))
                        {
                            widget->ProcessEvent(remove, nullptr);
                        }
                        return false;
                    }
                    watched_widget.store(reinterpret_cast<uintptr_t>(widget),
                                         std::memory_order_release);
                    added = true;
                    ++widgets_created;
                    proof_ui_create_failure.clear();
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] RuntimeWorldBootstrap operation=proof_ui status=added "
                            "widget={}\n"),
                        format_object_debug_name(widget));
                }
                else
                {
                    report_failure(STR("proof_ui"), proof_ui_create_failure, failure);
                }
            }
            return added;
        }
        auto process_events() -> bool
        {
            bool dirty = false;
            if (widget_deleted.exchange(false, std::memory_order_acq_rel) &&
                !live_weak_object(proof_ui))
            {
                proof_ui = nullptr;
                watched_widget.store(0);
                added = false;
                next_proof_ui_create_attempt_at = {};
                dirty = true;
            }
            if (widget_removed.exchange(false, std::memory_order_acq_rel))
            {
                viewport_check_pending = true;
                next_proof_ui_create_attempt_at = {};
                dirty = true;
            }
            return dirty;
        }
        FWeakObjectPtr proof_ui;
        bool added{};
        std::chrono::milliseconds retry_interval;
        std::chrono::steady_clock::time_point next_proof_ui_create_attempt_at{};
        std::atomic<uintptr_t> watched_widget{};
        std::atomic<bool> widget_deleted{}, widget_removed{};
        bool viewport_check_pending{}, delete_listener_installed{}, remove_hook_installed{};
        std::pair<int, int> remove_hook_ids{};
        std::wstring lifecycle_failure;
        std::wstring proof_ui_create_failure;
        uint64_t widgets_created{};
    };
    ProofUiLifecycle::ProofUiLifecycle(std::chrono::milliseconds retry_interval)
        : impl_(std::make_unique<Impl>(retry_interval))
    {
    }
    ProofUiLifecycle::~ProofUiLifecycle() = default;
    auto ProofUiLifecycle::install_lifecycle_events() -> bool
    {
        return impl_->install_lifecycle_events();
    }
    auto ProofUiLifecycle::process_events() -> bool
    {
        return impl_->process_events();
    }
    auto ProofUiLifecycle::refresh_viewport() -> bool
    {
        return impl_->refresh_viewport();
    }
    auto ProofUiLifecycle::ensure_widget(UObject* pawn, UClass* type,
                                         std::chrono::steady_clock::time_point now) -> bool
    {
        return impl_->ensure_widget(pawn, type, now);
    }
    auto ProofUiLifecycle::reset(bool clear_events) -> void
    {
        (void)impl_->set_battle_level_map_visible(false);
        impl_->retire_proof_ui();
        impl_->added = false;
        impl_->viewport_check_pending = false;
        impl_->next_proof_ui_create_attempt_at = {};
        impl_->proof_ui_create_failure.clear();
        if (clear_events)
        {
            impl_->widget_deleted = false;
            impl_->widget_removed = false;
        }
    }
    auto ProofUiLifecycle::request_viewport_check() -> void
    {
        impl_->viewport_check_pending = true;
    }
    auto ProofUiLifecycle::set_battle_level_map_visible(bool visible) -> bool
    {
        return impl_->set_battle_level_map_visible(visible);
    }
    auto ProofUiLifecycle::widget() const -> UObject*
    {
        return live_weak_object(impl_->proof_ui);
    }
    auto ProofUiLifecycle::added() const -> bool
    {
        return impl_->added;
    }
    auto ProofUiLifecycle::lifecycle_installed() const -> bool
    {
        return impl_->delete_listener_installed && impl_->remove_hook_installed;
    }
    auto ProofUiLifecycle::widgets_created() const -> uint64_t
    {
        return impl_->widgets_created;
    }
    auto ProofUiLifecycle::in_viewport(UObject* widget) -> std::optional<bool>
    {
        return widget_in_viewport(widget);
    }
    auto ProofUiLifecycle::remove_widget() -> bool
    {
        auto* object = widget();
        auto* function = object ? widget_remove_function(object) : nullptr;
        if (!function)
        {
            return false;
        }
        object->ProcessEvent(function, nullptr);
        return true;
    }
} // namespace kh3ap
