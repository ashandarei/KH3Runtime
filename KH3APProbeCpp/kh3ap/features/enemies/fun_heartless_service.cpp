#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/features/enemies/fun_heartless_service.h"
#include "kh3ap/features/enemies/enemy_remap_service.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr std::array<int32_t, 6> decorative_crowd_mesh_indices{0, 3, 6, 9, 11, 13};
        constexpr auto decorative_shadow_replacement_mesh_index = 3;
        constexpr uint32_t fun_actor_guid_namespace = 0x4b483341;
        constexpr uint32_t fun_actor_guid_version = 0x46554e31;

        constexpr auto max_fun_heartless_replacements_per_update = 4;
        constexpr auto flantastic_reward_threshold = 17;
        constexpr auto flan_collision_scale = 1.5f;
        constexpr int maximum_entity_group_names = 16;
        constexpr size_t expected_unused_shadow_count = 8;
        constexpr size_t expected_ex010_opening_count = 20;
        constexpr size_t expected_ex101_opening_count = 8;
        constexpr size_t expected_ex203_opening_count = 2;

        constexpr auto heartless_update_interval = std::chrono::milliseconds(200);
        constexpr auto heartless_settings_refresh_interval = std::chrono::seconds(1);
        constexpr auto heartless_spawn_delay = std::chrono::seconds(1);
        constexpr int shadow_refill_slot_count = 100;
        constexpr double full_circle_radians = 6.283185307179586;
        constexpr int shadow_ring_radius_cm = 2450;
        constexpr int shadow_spawn_height_cm = 400;
        constexpr int enemy_spawn_grid_columns = 5;
        constexpr int enemy_spawn_grid_spacing_cm = 35;
    } // namespace

    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto encounter =
            L"/Game/Levels/kg/kg_01/umap/kg_01_enemy_01.kg_01_enemy_01:PersistentLevel.";
        constexpr auto spawn_path = L"/Script/TresGame.TresLevelScriptActor:SpawnLevelEntities";
        constexpr auto unused_shadows = L"KH3AP_UnusedOpeningShadows";
        std::atomic<bool> serialized_patch_enabled{};
        std::optional<FName> unused_shadow_group;
        struct Array
        {
            void* data;
            int32_t count, capacity;
        };
        std::wstring error_text(const char* text)
        {
            const std::string s(text);
            return {s.begin(), s.end()};
        }
        FProperty* property(UObject* object, const wchar_t* name)
        {
            if (!is_uobject_valid_fast(object))
            {
                throw std::runtime_error("invalid encounter object");
            }
            auto* p = object->GetPropertyByNameInChain(name);
            if (!p)
            {
                throw std::runtime_error("missing encounter property");
            }
            return p;
        }
        template <class T> T& value(UObject* object, const wchar_t* name)
        {
            auto* p = property(object, name);
            if (p->GetElementSize() != sizeof(T))
            {
                throw std::runtime_error("encounter property size mismatch");
            }
            return *p->ContainerPtrToValuePtr<T>(object);
        }
        void copy(UObject* to, UObject* from, const wchar_t* name)
        {
            auto* p = property(from, name);
            p->CopyCompleteValue_InContainer(to, from);
        }
        struct Call
        {
            UObject* object;
            UFunction* fn;
            std::vector<std::byte> params;
            Call(UObject* obj, const wchar_t* name)
                : object(obj), fn(obj->GetFunctionByNameInChain(name))
            {
                if (!is_uobject_valid_fast(fn))
                {
                    throw std::runtime_error("missing encounter function");
                }
                params.resize(std::max<int>(1, fn->GetParmsSize()));
            }
            void name(const wchar_t* field, std::wstring_view v)
            {
                if (!write_name_or_string_function_param(find_function_param_by_name(fn, field),
                                                         params.data(), v))
                {
                    throw std::runtime_error("encounter name parameter");
                }
            }
            void integer(const wchar_t* field, int32_t v)
            {
                if (!write_integral_function_param(find_function_param_by_name(fn, field),
                                                   params.data(), v))
                {
                    throw std::runtime_error("encounter integer parameter");
                }
            }
            void boolean(const wchar_t* field, bool v)
            {
                if (!write_bool_function_param(find_function_param_by_name(fn, field),
                                               params.data(), v))
                {
                    throw std::runtime_error("encounter bool parameter");
                }
            }
            void run()
            {
                object->ProcessEvent(fn, params.data());
            }
        };
        std::vector<std::wstring> groups(UObject* entity)
        {
            auto& a = value<Array>(entity, L"m_GroupNames");
            if (a.count < 0 || a.count > maximum_entity_group_names)
            {
                throw std::runtime_error("unexpected entity groups");
            }
            std::vector<std::wstring> result;
            for (int i = 0; i < a.count; ++i)
            {
                result.push_back(static_cast<FName*>(a.data)[i].ToString());
            }
            return result;
        }
        void set_groups(UObject* entity, const std::vector<std::wstring>& names)
        {
            auto& a = value<Array>(entity, L"m_GroupNames");
            a.data =
                ue_realloc_for_script_array(a.data, names.size() * sizeof(FName), alignof(FName));
            if (!a.data)
            {
                throw std::runtime_error("group allocation failed");
            }
            a.count = a.capacity = static_cast<int32_t>(names.size());
            for (size_t i = 0; i < names.size(); ++i)
            {
                static_cast<FName*>(a.data)[i] = FName(names[i].c_str(), FNAME_Add);
            }
        }
    } // namespace

    bool prepare_fun_heartless_serialized_entity(UObject* entity) noexcept
    {
        if (!serialized_patch_enabled.load(std::memory_order_acquire))
        {
            return false;
        }
        try
        {
            if (!is_uobject_valid_fast(entity) || !entity->GetPathName().starts_with(encounter))
            {
                return false;
            }
            auto& a = value<Array>(entity, L"m_GroupNames");
            if (a.count == 1 &&
                static_cast<FName*>(a.data)[0].ToString() == L"kg_01_en_Start_ex001")
            {
                static_cast<FName*>(a.data)[0] = *unused_shadow_group;
            }
            return true;
        }
        catch (...)
        {
        }
        return false;
    }

    struct RuntimeFunHeartlessService::Impl
    {
        RuntimeFunHeartlessConfig config;
        using Thunk = void (*)(UObject*, FFrame&, void*);
        Thunk original{};
        FWeakObjectPtr function, script, crowd;
        static inline Impl* instance{};
        bool loaded{}, enabled{}, preparing{}, failed{};
        Clock::time_point next{}, next_settings{};
        struct Slot
        {
            FWeakObjectPtr entity;
            std::wstring group;
            Clock::time_point due{};
            bool pending{};
        };
        std::vector<Slot> shadows;
        uint32_t serial{};
        explicit Impl(RuntimeFunHeartlessConfig c) : config(std::move(c))
        {
            instance = this;
        }
        ~Impl()
        {
            serialized_patch_enabled.store(false, std::memory_order_release);
            restore_crowd();
            if (auto* fn = static_cast<UFunction*>(function.Get());
                is_uobject_valid_fast(fn) && fn->GetFuncPtr() == &spawn_thunk)
            {
                fn->SetFuncPtr(original);
            }
            instance = nullptr;
        }
        void restore_crowd()
        {
            if (auto* module = crowd.Get(); is_uobject_valid_fast(module))
            {
                auto& indices = value<Array>(module, L"SpawnMeshDataIndex");
                if (indices.count == decorative_crowd_mesh_indices.size() &&
                    static_cast<int32_t*>(indices.data)[0] ==
                        decorative_shadow_replacement_mesh_index)
                {
                    static_cast<int32_t*>(indices.data)[0] = 0;
                }
            }
            crowd = nullptr;
        }
        void patch_crowd()
        {
            if (is_uobject_valid_fast(crowd.Get()))
            {
                return;
            }
            auto* module = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr,
                L"/Game/Effects/level/kg/kg_01/task/par/"
                L"vt_kg01_hrd_emy0.vt_kg01_hrd_emy0:ParticleModuleTypeDataSQEX_GPUParticle_1");
            if (!is_uobject_valid_fast(module))
            {
                return;
            }
            auto& indices = value<Array>(module, L"SpawnMeshDataIndex");
            const auto& expected = decorative_crowd_mesh_indices;
            if (indices.count != decorative_crowd_mesh_indices.size() ||
                std::memcmp(indices.data, expected.data(), sizeof(expected)))
            {
                return;
            }
            static_cast<int32_t*>(indices.data)[0] = decorative_shadow_replacement_mesh_index;
            crowd = module;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] FunHeartless decorative Shadow slot redirected in memory\n"));
        }
        static void spawn_thunk(UObject* context, FFrame& frame, void* result)
        {
            auto& self = *instance;
            // The original thunk consumes the VM arguments unchanged. Preparation
            // only edits this encounter's loaded entities before its first spawn.
            if (self.enabled && !self.preparing && is_uobject_valid_fast(context) &&
                context->GetPathName().starts_with(encounter))
            {
                if (self.script.Get() != context)
                {
                    self.restore_crowd();
                    self.script = context;
                    self.shadows.clear();
                    self.failed = false;
                    self.preparing = true;
                    try
                    {
                        self.prepare(context);
                    }
                    catch (const std::exception& e)
                    {
                        self.failed = true;
                        Output::send<LogLevel::Error>(
                            STR("[KH3APProbeCpp] FunHeartless preparation failed: {}\n"),
                            error_text(e.what()));
                    }
                    self.preparing = false;
                }
            }
            self.original(context, frame, result);
        }
        UObject* clone(UObject* source, const std::vector<std::wstring>& names,
                       const FVector& position)
        {
            const FTransform transform{FQuat{0, 0, 0, 1}, position, FVector{1, 1, 1}};
            // Owner selects the source streamed level, so native entity registration
            // attaches the new slot to the same level manager and battle counters.
            auto* actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
                source, source->GetClassPrivate(), transform,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn, static_cast<AActor*>(source));
            if (!is_uobject_valid_fast(actor))
            {
                throw std::runtime_error("entity spawn failed");
            }
            for (auto* field :
                 {L"m_ActorClassPath", L"m_ActorClass", L"m_AppearInfo", L"m_UserData"})
            {
                copy(actor, source, field);
            }
            set_groups(actor, names);
            std::array<uint32_t, 4> guid{
                fun_actor_guid_namespace, fun_actor_guid_version,
                static_cast<uint32_t>(Clock::now().time_since_epoch().count()), ++serial};
            value<std::array<uint32_t, 4>>(actor, L"m_GUID") = guid;
            value<int32_t>(actor, L"m_MaxSpawnCount") = 1;
            auto* finished = UGameplayStatics::FinishSpawningActor(actor, transform);
            if (is_uobject_valid_fast(finished))
            {
                actor = finished;
            }
            if (!is_uobject_valid_fast(actor))
            {
                throw std::runtime_error("entity finish spawn failed");
            }
            return actor;
        }
        void prepare(UObject* owner)
        {
            auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresLevelEntity");
            TArray<AActor*> actors;
            UGameplayStatics::GetAllActorsOfClass(owner, klass, actors);
            std::map<std::wstring, std::vector<UObject*>> by_group;
            for (auto* actor : actors)
            {
                if (is_uobject_valid_fast(actor) && actor->GetPathName().starts_with(encounter))
                {
                    for (auto& group : groups(actor))
                    {
                        by_group[group].push_back(actor);
                    }
                }
            }
            // Refuse to stack with the old experimental map pak or other density mods.
            if (by_group[unused_shadows].size() != expected_unused_shadow_count ||
                !by_group[L"kg_01_en_Start_ex001"].empty() ||
                by_group[L"kg_01_en_Start_ex010"].size() != expected_ex010_opening_count ||
                by_group[L"kg_01_en_Start_ex101"].size() != expected_ex101_opening_count ||
                by_group[L"kg_01_en_Start_ex203"].size() != expected_ex203_opening_count)
            {
                throw std::runtime_error(
                    "expected vanilla 38-enemy opening; remove encounter map overrides");
            }
            auto* shadow = by_group[unused_shadows].front();
            auto* air = by_group[L"kg_01_en_Start_ex010"].front();
            for (auto* group : {L"kg_01_en_Start_ex203", L"kg_01_en_Loop_ex203"})
            {
                for (auto* actor : by_group[group])
                {
                    copy(actor, air, L"m_ActorClassPath");
                    copy(actor, air, L"m_ActorClass");
                }
            }
            for (int i = 0; i < shadow_refill_slot_count; ++i)
            {
                const auto group = L"KH3AP_ShadowRefill_" + std::to_wstring(i);
                const double angle = full_circle_radians * i / shadow_refill_slot_count;
                auto* entity = clone(shadow, {L"kg_01_en_Start_ex001", group},
                                     FVector{float(shadow_ring_radius_cm * std::cos(angle)),
                                             float(shadow_ring_radius_cm * std::sin(angle)),
                                             shadow_spawn_height_cm});
                Slot slot{};
                slot.entity = entity;
                slot.group = group;
                shadows.push_back(std::move(slot));
            }
            const std::map<std::wstring, int> limits{
                {L"kg_01_en_Start_ex010", 67},  {L"kg_01_en_Start_ex101", 27},
                {L"kg_01_en_Start_ex203", 6},   {L"kg_01_en_Loop_E_ex010", 13},
                {L"kg_01_en_Loop_E_ex081", 13}, {L"kg_01_en_Loop_ex010", 13},
                {L"kg_01_en_Loop_ex081", 13},   {L"kg_01_en_Loop_ex101", 11},
                {L"kg_01_en_Loop_ex113", 5},    {L"kg_01_en_Loop_ex203", 3}};
            for (auto& [group, count] : limits)
            {
                auto& sources = by_group.at(group);
                for (int i = static_cast<int>(sources.size()); i < count; ++i)
                {
                    auto* source = sources[i % sources.size()];
                    auto* root = try_read_reflected_object_property_value(source, L"RootComponent");
                    const auto pos = value<std::array<float, 3>>(root, L"RelativeLocation");
                    clone(source, {group},
                          FVector{pos[0] + float((i % enemy_spawn_grid_columns) *
                                                 enemy_spawn_grid_spacing_cm),
                                  pos[1] + float((i / enemy_spawn_grid_columns) *
                                                 enemy_spawn_grid_spacing_cm),
                                  pos[2]});
                }
            }
            auto* wall = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr,
                (std::wstring(encounter) + L"g_kg_01_invisiblewall_03_13").c_str());
            if (is_uobject_valid_fast(wall))
            {
                Call call(wall, L"SetActorScale3D");
                auto* p = find_function_param_by_name(call.fn, L"NewScale3D");
                const std::array<float, 3> scale{flan_collision_scale, flan_collision_scale, 1};
                if (!p || p->GetElementSize() != sizeof(scale))
                {
                    throw std::runtime_error("wall scale layout");
                }
                std::memcpy(p->ContainerPtrToValuePtr<void>(call.params.data()), scale.data(),
                            sizeof(scale));
                call.run();
            }
            auto* threshold = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr,
                (std::wstring(encounter) +
                 L"TresBattleVolume_A.TresLevelEntitySequence_1.TresLevelEntitySequenceAction_"
                 L"Sequence_0.TresLevelEntitySequence_0.TresLevelEntitySequenceCondition_Num_1")
                    .c_str());
            if (is_uobject_valid_fast(threshold))
            {
                value<int32_t>(threshold, L"m_Num") = flantastic_reward_threshold;
            }
            patch_crowd();
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] FunHeartless prepared shadows=100 "
                                                "normal=100 troll_slots=1 runtime_only=true\n"));
        }
        void spawn(UObject* owner, const std::wstring& group)
        {
            Call reset(owner, L"ResetLevelEntities");
            reset.name(L"TargetGroup", group);
            reset.run();
            Call call(owner, L"SpawnLevelEntities");
            call.name(L"TargetGroup", group);
            call.integer(L"AppearInfoNo", -1);
            call.integer(L"UserDataNo", -1);
            call.boolean(L"IncrementAppearInfoNo", true);
            call.boolean(L"IncrementUserDataNo", true);
            call.boolean(L"DelaySpawning", true);
            call.run();
        }
        void tick()
        {
            const auto now = Clock::now();
            if (!loaded && now >= next_settings)
            {
                next_settings = now + heartless_settings_refresh_interval;
                const auto& data = installed_seed_slot_data();
                loaded = true;
                enabled = data.valid() && seed_bool_option(data, "fun_10k_heartless");
                if (loaded && enabled)
                {
                    if (!ensure_runtime_level_entity_serialize_hook())
                    {
                        enabled = false;
                        return;
                    }
                    unused_shadow_group = FName(unused_shadows, FNAME_Add);
                    serialized_patch_enabled.store(true, std::memory_order_release);
                    auto* fn =
                        UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, spawn_path);
                    if (!is_uobject_valid_fast(fn))
                    {
                        loaded = false;
                        return;
                    }
                    original = fn->GetFuncPtr();
                    function = fn;
                    fn->SetFuncPtr(&spawn_thunk);
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] FunHeartless runtime enabled\n"));
                }
            }
            if (!enabled || failed || shadows.empty() || now < next)
            {
                return;
            }
            next = now + heartless_update_interval;
            auto* owner = script.Get();
            if (!is_uobject_valid_fast(owner))
            {
                restore_crowd();
                shadows.clear();
                return;
            }
            if (!config.battle_active())
            {
                restore_crowd();
                return;
            }
            auto* pawn = config.sora();
            if (!is_uobject_valid_fast(pawn) ||
                !pawn->GetPathName().starts_with(L"/Game/Levels/kg/kg_01/kg_01.kg_01:"))
            {
                return;
            }
            try
            {
                patch_crowd();
                size_t replacements = 0;
                for (auto& slot : shadows)
                {
                    auto* entity = slot.entity.Get();
                    if (!is_uobject_valid_fast(entity))
                    {
                        continue;
                    }
                    const int spawned = value<int32_t>(entity, L"m_SpawnCount"),
                              killed = value<int32_t>(entity, L"m_KillCount");
                    if (spawned <= 0 || killed < spawned)
                    {
                        continue;
                    }
                    if (!slot.pending)
                    {
                        slot.pending = true;
                        slot.due = now + heartless_spawn_delay;
                    }
                    if (now >= slot.due && replacements < max_fun_heartless_replacements_per_update)
                    {
                        spawn(owner, slot.group);
                        slot.pending = false;
                        ++replacements;
                    }
                }
            }
            catch (const std::exception& e)
            {
                failed = true;
                Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] FunHeartless refill stopped: {}\n"), error_text(e.what()));
            }
        }
    };
    RuntimeFunHeartlessService::RuntimeFunHeartlessService(RuntimeFunHeartlessConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }
    RuntimeFunHeartlessService::~RuntimeFunHeartlessService() = default;
    void RuntimeFunHeartlessService::tick()
    {
        impl_->tick();
    }
} // namespace kh3ap
