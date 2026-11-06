#include "kh3ap/features/story/save_points/save_point_discovery_service.h"

#include "kh3ap/features/story/save_points/save_point_spawn_helpers.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Script.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <array>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto save_point_destination_timeout = std::chrono::seconds(30);
        constexpr auto save_point_asset_retry_interval = std::chrono::seconds(2);
    }

    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        auto load_save_class(std::wstring_view path, std::wstring& status) -> UObject*
        {
            // AssetRegistryHelpers::GetAsset loads AssetData.ObjectPath through
            // the engine's normal loader, including cooked generated classes.
            auto* function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                STR("/Script/AssetRegistry.AssetRegistryHelpers:GetAsset"));
            status = L"missing_function";
            if (!is_uobject_valid_fast(function)) return nullptr;
            auto* self = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                STR("/Script/AssetRegistry.Default__AssetRegistryHelpers"));
            auto* data = find_function_param_by_name(function, STR("InAssetData"));
            status = L"missing_self_or_data";
            if (!is_uobject_valid_fast(self) || !data ||
                data->GetClass().GetName() != STR("StructProperty")) return nullptr;
            auto* field = find_struct_field_by_name(static_cast<FStructProperty*>(data)->GetStruct(), STR("ObjectPath"));
            status = L"missing_object_path";
            if (!field || function->GetParmsSize() <= 0) return nullptr;
            std::vector<uint8_t> params(function->GetParmsSize(), 0);
            auto* value = data->ContainerPtrToValuePtr<void>(params.data());
            status = L"init_failed";
            if (!initialize_property_value_dynamic(data, value)) return nullptr;
            UObject* loaded = nullptr;
            status = L"path_write_failed";
            if (write_name_or_string_function_param(field, value, path))
            {
                const RuntimeGrantInternalCallDepth::Scope scope{};
                self->ProcessEvent(function, params.data());
                loaded = try_read_function_object_param(function, params.data(), STR("ReturnValue"));
                status = is_uobject_valid_fast(loaded) ? L"loaded " + loaded->GetFullName() : L"load_returned_null";
                // Retain this single tiny serializer class for the process,
                // matching the runtime portal class lifetime policy.
                if (is_uobject_valid_fast(loaded)) loaded->SetRootSet();
            }
            destroy_property_value_dynamic(data, value);
            return loaded;
        }

        auto bool_property(UObject* actor, const wchar_t* name) -> bool
        {
            auto* property = actor->GetPropertyByNameInChain(name);
            return property && property->GetClass().GetName() == STR("BoolProperty") &&
                static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(actor);
        }

        auto accessed(UObject* actor, UObject* pawn, std::wstring& status) -> bool
        {
            // The save actor's transient IsAccessed bit is not an arrival
            // ledger. Use Sora's actual overlap with its active recovery volume.
            status = STR("savepoint_inactive");
            if (!bool_property(actor, STR("m_bIsActive"))) return false;
            auto* recovery = try_read_reflected_object_property_value(actor, STR("MyRecover"));
            status = STR("recovery_unavailable");
            if (!is_uobject_valid_fast(recovery)) return false;
            status = STR("recovery_inactive");
            if (!bool_property(recovery, STR("m_bIsGimmickActive"))) return false;
            status = STR("overlap_function_unavailable");
            auto* function = recovery->GetFunctionByNameInChain(STR("IsOverlappingActor"));
            if (!is_uobject_valid_fast(function)) return false;
            auto* result = find_function_param_by_name(function, STR("ReturnValue"));
            if (!result || result->GetClass().GetName() != STR("BoolProperty")) return false;
            std::vector<uint8_t> params(function->GetParmsSize(), 0);
            if (!write_object_function_param(function, params.data(), STR("Other"), pawn)) return false;
            const RuntimeGrantInternalCallDepth::Scope scope{};
            recovery->ProcessEvent(function, params.data());
            const bool arrived = static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(params.data());
            status = arrived ? STR("sora_overlapping") : STR("not_overlapping");
            return arrived;
        }

        auto player_start(UObject* actor) -> std::wstring
        {
            auto* property = actor->GetPropertyByNameInChain(STR("PlayerStartTag"));
            auto* value = actor->GetValuePtrByPropertyNameInChain(STR("PlayerStartTag"));
            if (!property || !value || property->GetClass().GetName() != STR("NameProperty") ||
                property->GetElementSize() != sizeof(FName)) return {};
            return static_cast<FName*>(value)->ToString();
        }
    }

    struct RuntimeSavePointDiscoveryService::Impl
    {
        struct Destination
        {
            const wchar_t* map;
            const wchar_t* tag;
            const wchar_t* slot;
            FWeakObjectPtr pending_actor;
            FWeakObjectPtr recorded_actor;
            bool pending{};
            std::chrono::steady_clock::time_point expires{};
            std::chrono::steady_clock::time_point retry_at{};
        };
        std::array<Destination, 2> destinations{{
            {L"/Game/Levels/mi/mi_03/mi_03", L"mi_03_Lv_Save_02", L"SavePointMIDiscoveredTankYard"},
            {L"/Game/Levels/mi/mi_04/mi_04", L"mi_04_Lv_Save_02", L"SavePointMIDiscoveredServiceArea"},
        }};
        struct Hook { std::wstring path; std::pair<int, int> ids; };
        std::vector<Hook> hooks;
        std::optional<std::filesystem::path> bridge;
        bool installed{};

        ~Impl()
        {
            for (const auto& hook : hooks)
            {
                try { UObjectGlobals::UnregisterHook(hook.path, hook.ids); }
                catch (...) {}
            }
        }

        auto enqueue(UObject* recovery) -> void
        {
            if (!is_uobject_valid_fast(recovery) ||
                recovery->GetClassPrivate()->GetName() != STR("TresSavePointRecoverComponent")) return;
            auto* actor = recovery->GetOuterPrivate();
            if (!is_uobject_valid_fast(actor) ||
                try_read_reflected_object_property_value(actor, STR("MyRecover")) != recovery) return;
            const auto tag = player_start(actor);
            for (auto& destination : destinations)
            {
                if (tag != destination.tag) continue;
                const std::wstring map{destination.map};
                if (actor->GetFullName().find(map.substr(0, map.rfind(L'/') + 1)) == std::wstring::npos) continue;
                destination.pending_actor = actor;
                destination.pending = true;
                destination.expires = std::chrono::steady_clock::now() + save_point_destination_timeout;
                destination.retry_at = {};
            }
        }

        auto install_once() -> void
        {
            if (installed) return;
            installed = true;
            // Register before slot loading so initial overlaps are observed too.
            // Activation covers a recovery volume enabled around an existing pawn.
            for (const auto* path : {
                L"/Script/TresGame.TresPawnOverlapComponentBase:OnBodyBeginOverlap",
                L"/Script/TresGame.TresGimmickComponentBase:BP_ActivateGimmickComponent"})
            {
                try
                {
                    auto ids = UObjectGlobals::RegisterHook(path,
                        [](UnrealScriptFunctionCallableContext&, void*) {},
                        [this](UnrealScriptFunctionCallableContext& context, void*) {
                            // Defer saving out of collision/delegate dispatch.
                            enqueue(get_frame_object(context.TheStack));
                        }, nullptr);
                    hooks.push_back({path, ids});
                }
                catch (...)
                {
                    Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SavePointDiscovery hook_failed={}\n"), path);
                }
            }
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] SavePointDiscovery mode=events hooks={}/2\n"), hooks.size());
        }

        auto has_pending_work() const -> bool
        {
            return destinations[0].pending || destinations[1].pending;
        }

        auto process(const RuntimeSavePointSpawnDeps& deps,
                     std::chrono::steady_clock::time_point now) -> void
        {
            if (bridge != deps.bridge_dir)
            {
                bridge = deps.bridge_dir;
                for (auto& destination : destinations) destination.recorded_actor = nullptr;
            }
            for (auto& destination : destinations)
            {
                if (!destination.pending) continue;
                auto* actor = destination.pending_actor.Get();
                if (!is_uobject_valid_fast(actor) || now >= destination.expires)
                {
                    destination.pending = false;
                    continue;
                }
                if (!bridge || now < destination.retry_at) continue;
                auto& candidate = deps.runtime_state.grant_state().pawn_state();
                auto* pawn = candidate.object();
                if (!candidate.candidate_trusted() || !is_uobject_valid_fast(pawn)) continue;
                destination.pending = false;
                if (pawn->GetFullName().find(destination.map) == std::wstring::npos ||
                    destination.recorded_actor.Get() == actor) continue;
                RuntimeConditionalProfileScope<KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0> profile{STR("save_point_discovery:event")};
                std::wstring status;
                // An overlap event from a companion is not discovery. Validate
                // Sora's actual overlap once, after the native event completes.
                if (!accessed(actor, pawn, status))
                {
                    Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] SavePointDiscovery event tag={} arrival={}\n"), destination.tag, status);
                    continue;
                }
                auto* save_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                    std::wstring{deps.goa_save_point_data_class_path}.c_str());
                if (!is_uobject_valid_fast(save_class))
                {
                    load_save_class(deps.goa_save_point_data_class_path, status);
                    Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] SavePointDiscovery class_load={}\n"), status);
                }
                GoASavePointRouteSyncSummary summary;
                if (sync_goa_save_point_route(deps, destination.slot, STR("MILevelPath"),
                    STR("MIPlayerStart"), destination.map, destination.tag, &summary))
                {
                    destination.recorded_actor = actor;
                    Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] SavePointDiscovery event tag={} status={}\n"), destination.tag, summary.status);
                }
                else
                {
                    // Only a failed event-triggered write can schedule bounded
                    // retries; idle save points never request overlap checks.
                    destination.pending = true;
                    destination.retry_at = now + save_point_asset_retry_interval;
                    Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] SavePointDiscovery event tag={} waiting={}\n"), destination.tag, summary.reason);
                }
            }
        }
    };

    RuntimeSavePointDiscoveryService::RuntimeSavePointDiscoveryService() : impl_(std::make_unique<Impl>()) {}
    RuntimeSavePointDiscoveryService::~RuntimeSavePointDiscoveryService() = default;
    auto RuntimeSavePointDiscoveryService::install_once() -> void { impl_->install_once(); }
    auto RuntimeSavePointDiscoveryService::has_pending_work() const -> bool { return impl_->has_pending_work(); }
    auto RuntimeSavePointDiscoveryService::process(const RuntimeSavePointSpawnDeps& deps,
        std::chrono::steady_clock::time_point now) -> void { impl_->process(deps, now); }
}
