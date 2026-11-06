#include <memory>
#include "kh3ap/features/story/save_points/goa_autosave_service.h"

#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <chrono>
#include <string>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto goa_autosave_settle_interval = std::chrono::seconds{2};
        constexpr auto max_autosave_outer_depth = 8;
        constexpr std::wstring_view goa_01_map_path =
            STR("/Game/Maps/GardenOfAssemblage/umap/GoA_01");
    } // namespace

    struct RuntimeGoaAutosaveService::Impl
    {
        explicit Impl(RuntimeGoaAutosaveConfig value) : config(std::move(value))
        {
        }

        [[nodiscard]] auto is_goa_01_context_active() const -> bool
        {
            const auto snapshot =
                config.runtime_state.context_subsystem_state().context_state().current_snapshot();
            const auto is_goa_01_context = [](std::wstring_view value)
            {
                return value.find(goa_01_map_path) != std::wstring_view::npos;
            };
            return snapshot.has_value() &&
                   (is_goa_01_context(snapshot->world) || is_goa_01_context(snapshot->outer) ||
                    is_goa_01_context(snapshot->object));
        }

        auto queue_after_manual_save(std::wstring_view save_file_path, int32_t slot_number) -> void
        {
            if (slot_number <= 0 || !is_goa_01_context_active())
            {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if ((pending && pending_slot == slot_number &&
                 now - pending_detected_at < goa_autosave_settle_interval) ||
                (last_autosave_at.time_since_epoch().count() != 0 &&
                 last_autosave_slot == slot_number &&
                 now - last_autosave_at < goa_autosave_settle_interval))
            {
                return;
            }

            pending = true;
            pending_slot = slot_number;
            pending_path = save_file_path;
            pending_detected_at = now;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] GoAManualSaveAutosave queued slot={} trigger=CloseHandle:post "
                    "dispatch=next_game_tick path={}\n"),
                slot_number, save_file_path);
        }

        auto process_pending() -> void
        {
            if (!pending)
            {
                return;
            }

            const int32_t slot_number = pending_slot;
            const std::wstring save_file_path = pending_path;
            pending = false;
            pending_slot = -1;
            pending_path.clear();
            pending_detected_at = {};
            create_after_manual_save(save_file_path, slot_number);
        }

        auto create_after_manual_save(std::wstring_view save_file_path, int32_t slot_number) -> void
        {
            if (slot_number <= 0 || !is_goa_01_context_active())
            {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (last_autosave_at.time_since_epoch().count() != 0 &&
                last_autosave_slot == slot_number &&
                now - last_autosave_at < goa_autosave_settle_interval)
            {
                return;
            }

            UObject* const cached_sora_pawn =
                config.runtime_state.grant_state().pawn_state().object();
            UObject* persistent_level = nullptr;
            if (cached_sora_pawn && kh3ap::is_uobject_valid_fast(cached_sora_pawn) &&
                RuntimeObjectResolver::is_sora_ability_pawn_candidate(cached_sora_pawn))
            {
                int outer_depth = 0;
                for (UObject* outer = cached_sora_pawn->GetOuterPrivate();
                     outer && kh3ap::is_uobject_valid_fast(outer) &&
                     outer_depth < max_autosave_outer_depth;
                     outer = outer->GetOuterPrivate(), ++outer_depth)
                {
                    if (outer->GetName() == STR("PersistentLevel") &&
                        outer->GetFullName().find(goa_01_map_path) != std::wstring::npos)
                    {
                        persistent_level = outer;
                        break;
                    }
                }
            }

            UObject* const level_script_actor =
                persistent_level
                    ? try_read_object_property_object(persistent_level, STR("LevelScriptActor"))
                    : nullptr;
            UFunction* const function_object =
                level_script_actor && kh3ap::is_uobject_valid_fast(level_script_actor)
                    ? level_script_actor->GetFunctionByNameInChain(STR("AP_CreateAutoSave"))
                    : nullptr;
            if (!function_object || !level_script_actor ||
                !kh3ap::is_uobject_valid_fast(level_script_actor))
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] GoAManualSaveAutosave skipped slot={} "
                        "reason=blueprint_wrapper_unavailable sora={} level={} level_script={} "
                        "function={} path={}\n"),
                    slot_number, format_object_debug_name(cached_sora_pawn),
                    format_object_debug_name(persistent_level),
                    format_object_debug_name(level_script_actor),
                    format_object_debug_name(function_object), save_file_path);
                return;
            }

            const int32_t params_size = function_object->GetParmsSize();
            if (params_size != 0)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] GoAManualSaveAutosave skipped slot={} "
                        "reason=blueprint_wrapper_has_parameters params_size={} path={}\n"),
                    slot_number, params_size, save_file_path);
                return;
            }

            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] GoAManualSaveAutosave dispatch begin slot={} "
                        "function=AP_CreateAutoSave level_script={} path={}\n"),
                    slot_number, format_object_debug_name(level_script_actor), save_file_path);
                level_script_actor->ProcessEvent(function_object, nullptr);
            }
            last_autosave_at = now;
            last_autosave_slot = slot_number;
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] GoAManualSaveAutosave called "
                                                "AP_CreateAutoSave slot={} path={}\n"),
                                            slot_number, save_file_path);
        }

        RuntimeGoaAutosaveConfig config;
        bool pending{false};
        int32_t pending_slot{-1};
        std::wstring pending_path{};
        std::chrono::steady_clock::time_point pending_detected_at{};
        std::chrono::steady_clock::time_point last_autosave_at{};
        int32_t last_autosave_slot{-1};
    };

    RuntimeGoaAutosaveService::RuntimeGoaAutosaveService(RuntimeGoaAutosaveConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeGoaAutosaveService::~RuntimeGoaAutosaveService() = default;
    RuntimeGoaAutosaveService::RuntimeGoaAutosaveService(RuntimeGoaAutosaveService&&) noexcept =
        default;
    auto RuntimeGoaAutosaveService::operator=(RuntimeGoaAutosaveService&&) noexcept
        -> RuntimeGoaAutosaveService& = default;

    auto RuntimeGoaAutosaveService::is_goa_01_context_active() const -> bool
    {
        return impl_->is_goa_01_context_active();
    }

    auto RuntimeGoaAutosaveService::queue_after_manual_save(std::wstring_view save_file_path,
                                                            int32_t slot_number) -> void
    {
        impl_->queue_after_manual_save(save_file_path, slot_number);
    }

    auto RuntimeGoaAutosaveService::process_pending() -> void
    {
        impl_->process_pending();
    }
} // namespace kh3ap
