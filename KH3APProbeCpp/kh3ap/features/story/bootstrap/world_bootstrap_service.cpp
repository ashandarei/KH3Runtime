#include <chrono>
#include <memory>
#include "kh3ap/features/story/bootstrap/world_bootstrap_calls.h"
#include "kh3ap/features/story/bootstrap/bootstrap_operation_timer.h"
#include "kh3ap/features/story/bootstrap/proof_ui_lifecycle.h"
#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap_service.h"
#include "kh3ap/features/story/progression/event_tracking_service.h"
#include "kh3ap/features/story/progression/vbonus_tracking_service.h"

#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/owned_function_cache.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/player/context/player_context_service.h"
#include "kh3ap/features/world/portals/portal_spawn_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Transform.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObjectArray.hpp>
#include <UnrealDef.hpp>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace bootstrap_native;

    namespace
    {
        using ::kh3ap::settings::s_enable_runtime_hot_path_profiling;

        auto live_weak_object(const FWeakObjectPtr& weak) -> UObject*
        {
            UObject* const object = weak.Get();
            return object && is_uobject_valid_fast(object) ? object : nullptr;
        }

        [[nodiscard]] auto bootstrap_class_ready(UClass* generated_class) -> bool
        {
            constexpr auto pending_load_flags = static_cast<EObjectFlags>(
                RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects);
            if (!is_uobject_valid_fast(generated_class) ||
                generated_class->HasAnyFlags(pending_load_flags))
            {
                return false;
            }
            // The creation observer can retain a Blueprint class before its
            // functions finish PostLoad/Bind. FinishSpawningActor immediately
            // invokes these events; a valid UObject with a null Func pointer
            // is not yet callable (observed during native autosave startup).
            for (const auto* event_name : {STR("UserConstructionScript"), STR("ReceiveBeginPlay")})
            {
                UFunction* const function = generated_class->GetFunctionByNameInChain(event_name);
                if (function &&
                    (!is_uobject_valid_fast(function) ||
                     function->HasAnyFlags(pending_load_flags) || !function->GetFuncPtr()))
                {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    struct RuntimeWorldBootstrapService::Impl
    {
        struct ActorState
        {
            RC::Unreal::FWeakObjectPtr actor{};
            std::chrono::steady_clock::time_point next_attempt_at{};
            std::wstring last_failure{};
        };

        struct ObservedClassState
        {
            RuntimePortalClassCache cache{};
            std::chrono::steady_clock::time_point next_attempt_at{};
            std::wstring last_failure{};
        };

        explicit Impl(RuntimeWorldBootstrapConfig value)
            : config(std::move(value)), proof_ui(config.asset_load_retry_interval),
              event_tracking(config.bridge_dir_resolver),
              vbonus_tracking(config.bridge_dir_resolver)
        {
        }

        auto reset() -> void
        {
            proof_ui.reset(true);
            event_tracking.reset();
            vbonus_tracking.reset();
            ready_pawn = nullptr;
            ready_world = nullptr;
            ready_controller = nullptr;
            world = nullptr;
            actors = {};
            progress.reset();
            work.request();
            next_class_scan_at = {};
            loading_failure.clear();
            shop_failure.clear();
        }

        auto begin_world(RC::Unreal::UObject* current_world) -> void
        {
            proof_ui.reset(false);
            world = make_uobject_weak(current_world);
            actors = {};
            progress.reset();
            (void)progress.observe_world(reinterpret_cast<std::uintptr_t>(current_world));
            loading_failure.clear();
            shop_failure.clear();
            if (config.debug_logging)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeWorldBootstrap status=new_world world=0x{:X}\n"),
                    reinterpret_cast<std::uintptr_t>(current_world));
            }
        }

        auto retain_observed_class(ObservedClassState& state, UClass* generated_class,
                                   std::wstring_view operation, std::wstring_view source) -> bool
        {
            if (!is_uobject_a_fast(generated_class, UClass::StaticClass()))
            {
                return false;
            }
            const bool newly_retained = state.cache.retain(generated_class);
            if (state.cache.retained_class() != generated_class)
            {
                return false;
            }

            state.next_attempt_at = {};
            state.last_failure.clear();
            if (newly_retained)
            {
                work.request();
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeWorldBootstrap operation={} status=class_ready "
                        "class={} source={} rooted=true\n"),
                    operation, format_object_debug_name(generated_class), source);
            }
            return true;
        }

        auto cache_generated_class(UObject* object) -> bool
        {
            if (!is_uobject_a_fast(object, UClass::StaticClass()))
            {
                return false;
            }
            std::wstring full_name{};
            if (!try_get_uobject_full_name_fast(object, full_name))
            {
                return false;
            }

            auto* const generated_class = static_cast<UClass*>(object);
            for (size_t index = 0; index < runtime_required_world_actors.size(); ++index)
            {
                const RuntimeWorldActorDefinition& definition =
                    runtime_required_world_actors[index];
                if (full_name.find(definition.class_path) != std::wstring::npos)
                {
                    return retain_observed_class(actor_classes[index], generated_class,
                                                 definition.name, STR("UObjectCreate"));
                }
            }
            if (full_name.find(runtime_proof_ui_class_path) != std::wstring::npos)
            {
                return retain_observed_class(proof_ui_class, generated_class, STR("proof_ui_class"),
                                             STR("UObjectCreate"));
            }
            return false;
        }

        auto refresh_loaded_classes(std::chrono::steady_clock::time_point now) -> void
        {
            if (next_class_scan_at != std::chrono::steady_clock::time_point{} &&
                now < next_class_scan_at)
            {
                return;
            }
            next_class_scan_at = now + config.asset_load_retry_interval;
            const BootstrapOperationTimer timing{STR("class_lookup_batch"), STR("support_classes")};
            // Resolve known paths through the engine's nonloading soft-reference
            // hash. A creation observer still handles newly arriving classes.
            for (const auto& definition : runtime_required_world_actors)
            {
                if (auto* type = find_loaded_runtime_asset_class(definition.class_path))
                {
                    cache_generated_class(type);
                }
            }
            if (auto* type = find_loaded_runtime_asset_class(runtime_proof_ui_class_path))
            {
                cache_generated_class(type);
            }
        }

        auto resolve_observed_class(ObservedClassState& state, UObject* world_context,
                                    std::wstring_view class_path, int32_t latent_uuid,
                                    std::wstring_view operation,
                                    std::chrono::steady_clock::time_point now) -> UClass*
        {
            if (UClass* const retained = state.cache.retained_class())
            {
                if (!bootstrap_class_ready(retained))
                {
                    report_failure(operation, state.last_failure, STR("class_postload_pending"));
                    return nullptr;
                }
                state.last_failure.clear();
                return retained;
            }

            // Creation observations above can resolve a class immediately.
            // A miss should not trigger another lookup on every 250 ms tick.
            if (state.next_attempt_at != std::chrono::steady_clock::time_point{} &&
                now < state.next_attempt_at)
            {
                return nullptr;
            }
            refresh_loaded_classes(now);
            if (UClass* const retained = state.cache.retained_class())
            {
                if (!bootstrap_class_ready(retained))
                {
                    report_failure(operation, state.last_failure, STR("class_postload_pending"));
                    return nullptr;
                }
                state.last_failure.clear();
                return retained;
            }
            state.next_attempt_at = now + config.asset_load_retry_interval;

            std::wstring resolution{};
            bool requested = false;
            {
                const BootstrapOperationTimer timing{STR("asset_request"), operation};
                requested = request_runtime_asset_class_load(world_context, class_path, &resolution,
                                                             latent_uuid);
            }
            if (!requested)
            {
                report_failure(operation, state.last_failure,
                               resolution.empty() ? std::wstring_view{STR("class_unavailable")}
                                                  : std::wstring_view{resolution});
                return nullptr;
            }
            state.last_failure.clear();
            if (config.debug_logging)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeWorldBootstrap operation={} "
                        "status=class_load_requested class={} source={}\n"),
                    operation, class_path, resolution);
            }
            return nullptr;
        }

        auto ensure_actor(size_t index, RC::Unreal::UObject* pawn,
                          std::chrono::steady_clock::time_point now) -> RC::Unreal::AActor*
        {
            ActorState& state = actors[index];
            if (UObject* const live_actor = live_weak_object(state.actor))
            {
                return static_cast<RC::Unreal::AActor*>(live_actor);
            }

            const RuntimeWorldActorDefinition& definition = runtime_required_world_actors[index];
            RC::Unreal::UClass* const actor_class =
                resolve_observed_class(actor_classes[index], pawn, definition.class_path,
                                       definition.latent_uuid, definition.name, now);
            if (!actor_class)
            {
                return nullptr;
            }
            if (state.next_attempt_at != std::chrono::steady_clock::time_point{} &&
                now < state.next_attempt_at)
            {
                return nullptr;
            }
            state.next_attempt_at = now + config.asset_load_retry_interval;

            std::vector<RC::Unreal::AActor*> existing{};
            {
                const BootstrapOperationTimer timing{STR("find_existing_actor"), definition.name};
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                RC::Unreal::UGameplayStatics::GetAllActorsOfClass(pawn, actor_class, existing);
            }
            for (RC::Unreal::AActor* const candidate : existing)
            {
                if (is_uobject_valid_fast(candidate))
                {
                    state.actor = candidate;
                    state.last_failure.clear();
                    return candidate;
                }
            }

            const RC::Unreal::FTransform spawn_transform{
                RC::Unreal::FQuat{0.0, 0.0, 0.0, 1.0},
                RC::Unreal::FVector{0.0, 0.0, 0.0},
                RC::Unreal::FVector{1.0, 1.0, 1.0},
            };
            RC::Unreal::AActor* spawned = nullptr;
            {
                const BootstrapOperationTimer timing{STR("begin_spawn"), definition.name};
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                spawned = RC::Unreal::UGameplayStatics::BeginDeferredActorSpawnFromClass(
                    pawn, actor_class, spawn_transform,
                    RC::Unreal::ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr);
            }
            if (!is_uobject_valid_fast(spawned))
            {
                report_failure(definition.name, state.last_failure,
                               STR("begin_deferred_spawn_returned_null"));
                return nullptr;
            }
            RC::Unreal::AActor* finished = nullptr;
            {
                const BootstrapOperationTimer timing{STR("finish_spawn"), definition.name};
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                finished =
                    RC::Unreal::UGameplayStatics::FinishSpawningActor(spawned, spawn_transform);
            }
            if (is_uobject_valid_fast(finished))
            {
                spawned = finished;
            }
            if (!is_uobject_valid_fast(spawned))
            {
                report_failure(definition.name, state.last_failure,
                               STR("finish_spawning_actor_invalidated_actor"));
                return nullptr;
            }

            state.actor = spawned;
            state.last_failure.clear();
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] RuntimeWorldBootstrap operation={} status=spawned actor={}\n"),
                definition.name, format_object_debug_name(spawned));
            return spawned;
        }

        auto prewarm_proof_ui_class(RC::Unreal::UObject* pawn,
                                    std::chrono::steady_clock::time_point now) -> UClass*
        {
            return resolve_observed_class(proof_ui_class, pawn, runtime_proof_ui_class_path,
                                          runtime_proof_ui_asset_load_uuid, STR("proof_ui_class"),
                                          now);
        }

        auto perform_bootstrap(std::chrono::steady_clock::time_point now) -> bool
        {
            UObject* const pawn = config.player_context.trusted_current_playable_cached_sora_pawn();
            if (!is_uobject_valid_fast(pawn) || !is_uobject_a_fast(pawn, AActor::StaticClass()))
            {
                return false;
            }
            auto* const pawn_actor = static_cast<AActor*>(pawn);
            UObject* const current_world = reinterpret_cast<UObject*>(pawn_actor->GetWorld());
            if (!is_uobject_valid_fast(current_world))
            {
                return false;
            }
            if (live_weak_object(world) != current_world)
            {
                begin_world(current_world);
            }

            for (size_t index = 0; index < runtime_required_world_actors.size(); ++index)
            {
                (void)ensure_actor(index, pawn, now);
            }

            if (!proof_ui.refresh_viewport())
            {
                return false;
            }
            progress.proof_ui_added = proof_ui.added();

            const bool bootstrap_effects_pending =
                !progress.shop_level_set || !progress.proof_ui_added;
            if (!bootstrap_effects_pending)
            {
                return true;
            }

            std::wstring failure{};
            const std::optional<bool> loading_closed = call_loading_screen_closed(&failure);
            if (!loading_closed.has_value())
            {
                report_failure(STR("loading_screen"), loading_failure, failure);
                return false;
            }
            loading_failure.clear();
            if (!*loading_closed)
            {
                return false;
            }

            if (!progress.shop_level_set)
            {
                failure.clear();
                if (call_set_shop_level(pawn, &failure))
                {
                    progress.shop_level_set = true;
                    shop_failure.clear();
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] RuntimeWorldBootstrap operation=shop_level "
                            "status=applied level={}\n"),
                        runtime_goa_shop_level);
                    // Give UMG construction a separate eligible update.
                    return false;
                }
                else
                {
                    report_failure(STR("shop_level"), shop_failure, failure);
                }
            }

            UClass* const widget_class =
                !proof_ui.added() ? prewarm_proof_ui_class(pawn, now) : nullptr;
            progress.proof_ui_added = proof_ui.ensure_widget(pawn, widget_class, now);
            return progress.complete();
        }

        auto world_ready() -> void
        {
            auto* const pawn = config.player_context.trusted_current_playable_cached_sora_pawn();
            if (!is_uobject_a_fast(pawn, AActor::StaticClass()))
            {
                return;
            }
            auto* const controller =
                try_read_reflected_object_property_value(pawn, STR("Controller"));
            if (!is_uobject_valid_fast(controller) ||
                try_read_reflected_object_property_value(controller, STR("Pawn")) != pawn)
            {
                return;
            }
            auto* const current_world =
                reinterpret_cast<UObject*>(static_cast<AActor*>(pawn)->GetWorld());
            if (!is_uobject_valid_fast(current_world))
            {
                return;
            }
            if (ready_pawn.Get() == pawn && ready_world.Get() == current_world &&
                ready_controller.Get() == controller)
            {
                return;
            }
            ready_pawn = make_uobject_weak(pawn);
            ready_world = make_uobject_weak(current_world);
            ready_controller = make_uobject_weak(controller);
            proof_ui.request_viewport_check();
            work.request();
            vbonus_tracking.world_ready();
            event_tracking.world_ready();
        }

        auto tick(std::chrono::steady_clock::time_point now) -> void
        {
            // These event inboxes remain independent of bootstrap sleeping.
            vbonus_tracking.process_events();
            event_tracking.process_events();
            if (!config.enabled)
            {
                return;
            }
            if (proof_ui.process_events())
            {
                progress.proof_ui_added = proof_ui.added();
                work.request();
            }
            if (!work.take(now))
            {
                return;
            }
            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                STR("on_update:world_bootstrap_work")};
            ++attempts;
            const bool complete = proof_ui.install_lifecycle_events() && perform_bootstrap(now);
            work.finish(now, complete, config.retry_interval);
        }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto diagnostic_snapshot(bool remove_widget) -> std::string
        {
            auto* widget = proof_ui.widget();
            if (remove_widget)
            {
                if (!widget || !progress.complete() || work.pending())
                {
                    throw std::runtime_error("bootstrap_not_ready");
                }
                if (!proof_ui.remove_widget())
                {
                    throw std::runtime_error("bootstrap_remove_layout");
                }
            }
            static OwnedFunctionCache shop_read{L"BP_TresGetShopLevel"};
            auto* fn =
                shop_read.resolve(find_loaded_runtime_asset(tres_blueprint_library_default_path));
            auto* owner = find_default_object(tres_blueprint_library_default_path, fn);
            auto* result = fn ? find_function_param_by_name(fn, L"ReturnValue") : nullptr;
            if (!owner || !result || result->GetClass().GetName() != L"IntProperty" ||
                result->GetElementSize() != sizeof(int32_t) || result->GetOffset_Internal() < 0 ||
                result->GetOffset_Internal() + sizeof(int32_t) > fn->GetParmsSize())
            {
                throw std::runtime_error("bootstrap_shop_read_layout");
            }
            std::vector<std::byte> params(fn->GetParmsSize());
            auto* pawn = config.player_context.trusted_current_playable_cached_sora_pawn();
            if (!write_object_function_param(fn, params.data(), L"WorldContextObject", pawn))
            {
                throw std::runtime_error("bootstrap_shop_read_context");
            }
            owner->ProcessEvent(fn, params.data());
            const int shop_level = *result->ContainerPtrToValuePtr<int32_t>(params.data());
            std::vector<UObject*> widgets;
            UObjectGlobals::FindAllOf(L"W_ProofUI_C", widgets);
            int visible = 0;
            for (auto* candidate : widgets)
            {
                if (is_uobject_valid_fast(candidate) &&
                    ProofUiLifecycle::in_viewport(candidate).value_or(false))
                {
                    ++visible;
                }
            }
            std::ostringstream json;
            json << "{\"complete\":" << (progress.complete() ? "true" : "false")
                 << ",\"pending\":" << (work.pending() ? "true" : "false")
                 << ",\"lifecycle_installed\":"
                 << (proof_ui.lifecycle_installed() ? "true" : "false")
                 << ",\"shop_level\":" << shop_level << ",\"in_viewport\":"
                 << (ProofUiLifecycle::in_viewport(widget).value_or(false) ? "true" : "false")
                 << ",\"widget_index\":" << (widget ? widget->GetInternalIndex() : -1)
                 << ",\"visible_widgets\":" << visible << ",\"attempts\":" << attempts
                 << ",\"widgets_created\":" << proof_ui.widgets_created() << '}';
            return json.str();
        }
#endif

        RuntimeWorldBootstrapConfig config;
        RC::Unreal::FWeakObjectPtr world{};
        RC::Unreal::FWeakObjectPtr ready_pawn{}, ready_world{}, ready_controller{};
        std::array<ActorState, runtime_required_world_actors.size()> actors{};
        std::array<ObservedClassState, runtime_required_world_actors.size()> actor_classes{};
        ObservedClassState proof_ui_class{};
        std::chrono::steady_clock::time_point next_class_scan_at{};
        RuntimeWorldBootstrapProgress progress{};
        ProofUiLifecycle proof_ui;
        RuntimeWorldBootstrapWork work;
        uint64_t attempts{};
        std::wstring loading_failure{};
        std::wstring shop_failure{};
        RuntimeEventTrackingService event_tracking;
        RuntimeVBonusTrackingService vbonus_tracking;
    };

    RuntimeWorldBootstrapService::RuntimeWorldBootstrapService(RuntimeWorldBootstrapConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeWorldBootstrapService::~RuntimeWorldBootstrapService() = default;
    RuntimeWorldBootstrapService::RuntimeWorldBootstrapService(
        RuntimeWorldBootstrapService&&) noexcept = default;
    auto RuntimeWorldBootstrapService::operator=(RuntimeWorldBootstrapService&&) noexcept
        -> RuntimeWorldBootstrapService& = default;

    auto RuntimeWorldBootstrapService::tick(std::chrono::steady_clock::time_point now) -> void
    {
        impl_->tick(now);
    }

    auto RuntimeWorldBootstrapService::world_ready() -> void
    {
        impl_->world_ready();
    }

    auto RuntimeWorldBootstrapService::reset() -> void
    {
        impl_->reset();
    }

    auto RuntimeWorldBootstrapService::cache_generated_class(RC::Unreal::UObject* object) -> bool
    {
        return impl_->cache_generated_class(object);
    }

    auto RuntimeWorldBootstrapService::set_battle_level_map_visible(bool visible) -> bool
    {
        return impl_->proof_ui.set_battle_level_map_visible(visible);
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeWorldBootstrapService::diagnostic_snapshot(bool remove_widget) -> std::string
    {
        return impl_->diagnostic_snapshot(remove_widget);
    }
#endif
} // namespace kh3ap
