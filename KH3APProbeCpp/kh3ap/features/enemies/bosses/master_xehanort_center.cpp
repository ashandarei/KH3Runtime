#include "master_xehanort_center.h"
#include "master_xehanort_center_policy.h"
#include "master_xehanort_queries.h"
#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <vector>
#include <Windows.h>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace enemy_remap_detail;
    namespace
    {
        struct ArenaOwner
        {
            FWeakObjectPtr pawn, entity, world;
            MasterArenaCenter center{};
        };
        std::mutex owners_mutex;
        std::vector<ArenaOwner> owners;
        std::atomic<bool> has_owners{};
        FName ascension_name{};
        FName master_name{};
        FName dark_shooting_name{};
        std::atomic<bool> configured{};
        constexpr size_t max_owners = 32;
        using Clock = std::chrono::steady_clock;
        struct PendingAscension
        {
            FWeakObjectPtr actor;
            Clock::time_point deadline, next_attempt;
            bool action{};
        };
        std::mutex pending_mutex;
        std::vector<PendingAscension> pending;
        std::atomic<size_t> pending_count{};
        constexpr size_t max_pending = 64;
        using PostInitProperties = void (*)(UObject*);
        std::atomic<PostInitProperties> original_dark_shooting_post_init{};
        std::unique_ptr<RuntimeBossHpLimitOverride> dark_shooting_post_init_override;

        auto vector_property(FProperty* field) -> bool
        {
            if (!field || field->GetClass().GetName() != L"StructProperty" ||
                field->GetElementSize() != sizeof(MasterArenaCenter))
            {
                return false;
            }
            auto* structure = static_cast<FStructProperty*>(field)->GetStruct();
            return is_uobject_valid_fast(structure) && structure->GetName() == L"Vector";
        }

        auto read_vector(UObject* object, const wchar_t* name) -> std::optional<MasterArenaCenter>
        {
            if (!is_uobject_valid_fast(object))
            {
                return std::nullopt;
            }
            auto* field = object->GetPropertyByNameInChain(name);
            if (!vector_property(field))
            {
                return std::nullopt;
            }
            MasterArenaCenter result{};
            std::memcpy(result.data(), field->ContainerPtrToValuePtr<void>(object), sizeof(result));
            return result;
        }

        auto arena_center(UObject* pawn) -> std::optional<MasterArenaCenter>
        {
            auto* function = pawn->GetFunctionByNameInChain(L"GetCenterLocation");
            auto* result = is_uobject_valid_fast(function)
                               ? find_function_param_by_name(function, L"ReturnValue")
                               : nullptr;
            if (!vector_property(result) || function->GetParmsSize() != sizeof(MasterArenaCenter) ||
                result->GetOffset_Internal() != 0)
            {
                return std::nullopt;
            }
            MasterArenaCenter center{};
            pawn->ProcessEvent(function, center.data());
            return translated_master_arena_center(center, MasterArenaCenter{});
        }

        auto trace_wall_distance(UObject* mesh, const MasterArenaCenter& start,
                                 const MasterArenaCenter& end) -> std::optional<float>
        {
            if (!is_uobject_valid_fast(mesh))
            {
                return std::nullopt;
            }
            auto* function = mesh->GetFunctionByNameInChain(L"K2_LineTraceComponent");
            alignas(std::max_align_t) std::array<std::byte, 256> params{};
            if (!is_uobject_valid_fast(function) || function->GetParmsSize() > params.size())
            {
                return std::nullopt;
            }
            const auto parameter = [&](std::wstring_view name) -> FProperty*
            {
                auto* field = find_function_param_by_name(function, name);
                return field && field->GetOffset_Internal() >= 0 && field->GetElementSize() > 0 &&
                               field->GetOffset_Internal() + field->GetElementSize() <=
                                   function->GetParmsSize()
                           ? field
                           : nullptr;
            };
            auto* trace_start = parameter(L"TraceStart");
            auto* trace_end = parameter(L"TraceEnd");
            auto* hit_location = parameter(L"HitLocation");
            auto* hit = parameter(L"ReturnValue");
            if (!vector_property(trace_start) || !vector_property(trace_end) ||
                !vector_property(hit_location) || !hit || !hit->IsA<FBoolProperty>())
            {
                return std::nullopt;
            }
            std::memcpy(trace_start->ContainerPtrToValuePtr<void>(params.data()), start.data(),
                        sizeof(start));
            std::memcpy(trace_end->ContainerPtrToValuePtr<void>(params.data()), end.data(),
                        sizeof(end));
            mesh->ProcessEvent(function, params.data());
            if (!static_cast<FBoolProperty*>(hit)->GetPropertyValueInContainer(params.data()))
            {
                return std::nullopt;
            }
            MasterArenaCenter location{};
            std::memcpy(location.data(), hit_location->ContainerPtrToValuePtr<void>(params.data()),
                        sizeof(location));
            const float distance = std::hypot(location[0] - start[0], location[1] - start[1]);
            const float length = std::hypot(end[0] - start[0], end[1] - start[1]);
            return std::isfinite(distance) && distance > 0 && distance <= length
                       ? std::optional{distance}
                       : std::nullopt;
        }

        auto constrain_dark_shooting_warp(UObject* pawn, const MasterArenaCenter& center,
                                          const MasterArenaCenter& destination) -> MasterArenaCenter
        {
            // This wall is shared by all phases of the verified base-game
            // Area B fixture. The larger TresBattleVolume does not represent
            // its playable boundary. Do not assume other arenas have this wall.
            constexpr auto area_b = L"/Game/Levels/kg/kg_02/umap/kg_02_enemy_Area_B."
                                    L"kg_02_enemy_Area_B:PersistentLevel";
            auto* level = static_cast<AActor*>(pawn)->GetLevel();
            if (!is_uobject_valid_fast(level) || level->GetPathName() != area_b)
            {
                return destination;
            }
            const float dx = destination[0] - center[0];
            const float dy = destination[1] - center[1];
            const float distance = std::hypot(dx, dy);
            if (distance == 0)
            {
                return destination;
            }
            const auto wall_path = std::wstring{area_b} + L".g_kg_02_battlewall_02_106";
            auto* wall =
                UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, wall_path.c_str());
            auto* mesh = is_uobject_valid_fast(wall) && wall->GetOuterPrivate() == level
                             ? read_boss_owner_property(wall, L"MyMesh")
                             : nullptr;
            const MasterArenaCenter start{center[0], center[1], destination[2]};
            // Extend past the requested warp to distinguish an in-bounds
            // request from an unavailable wall. Trace only the wall component.
            const float extension = (distance + 5000.0f) / distance;
            const MasterArenaCenter end{center[0] + dx * extension, center[1] + dy * extension,
                                        destination[2]};
            const auto contact = trace_wall_distance(mesh, start, end);
            constexpr float wall_clearance = 150.0f;
            return constrain_master_warp_xy(center, destination,
                                            (std::max)(0.0f, contact.value_or(0) - wall_clearance))
                .value_or(start);
        }

        auto translate(UObject* actor, const MasterArenaCenter& center,
                       const wchar_t* property_name = L"m_MapCenterPos",
                       UObject* warp_owner = nullptr) -> bool
        {
            auto* klass = actor->GetClassPrivate();
            auto* defaults =
                is_uobject_valid_fast(klass) ? klass->GetClassDefaultObject() : nullptr;
            const auto authored = read_vector(defaults, property_name);
            const auto before = read_vector(actor, property_name);
            auto desired =
                authored ? translated_master_arena_center(center, *authored) : std::nullopt;
            if (!before || !desired)
            {
                return false;
            }
            const auto translated = *desired;
            if (warp_owner)
            {
                *desired = constrain_dark_shooting_warp(warp_owner, center, translated);
            }
            auto* field = actor->GetPropertyByNameInChain(property_name);
            if (before == desired)
            {
                return true;
            }
            std::memcpy(field->ContainerPtrToValuePtr<void>(actor), desired->data(),
                        sizeof(*desired));
            if (read_vector(actor, property_name) != desired)
            {
                return false;
            }
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Master Xehanort arena center actor={} field={} "
                    "before=({},{},{}) after=({},{},{}) source=GetCenterLocation "
                    "authored_offset=({},{},{}) instance_only=true polling=false\n"),
                actor->GetPathName(), property_name, (*before)[0], (*before)[1], (*before)[2],
                (*desired)[0], (*desired)[1], (*desired)[2], (*authored)[0], (*authored)[1],
                (*authored)[2]);
            if (*desired != translated)
            {
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Master Xehanort Dark Shooting warp constrained "
                        "arena=kg_02_area_b horizontal_before={} horizontal_after={} "
                        "height_preserved=true polling=false\n"),
                    std::hypot(translated[0] - center[0], translated[1] - center[1]),
                    std::hypot((*desired)[0] - center[0], (*desired)[1] - center[1]));
            }
            return true;
        }

        auto owning_world(UObject* actor) -> UObject*
        {
            // AActor's wrapper walks typed outers, which yields the sublevel's
            // asset UWorld. Dynamic supports spawn in the persistent level;
            // ULevel.OwningWorld identifies their common live game world.
            auto* level = static_cast<AActor*>(actor)->GetLevel();
            return read_boss_owner_property(level, L"OwningWorld");
        }

        auto live_owner(const ArenaOwner& owner) -> bool
        {
            auto* pawn = owner.pawn.Get();
            auto* entity = owner.entity.Get();
            auto* world = owner.world.Get();
            return is_uobject_valid_fast(pawn) && is_uobject_valid_fast(entity) &&
                   is_uobject_valid_fast(world) && owning_world(pawn) == world &&
                   read_boss_owner_property(pawn, L"m_LevelEntity") == entity &&
                   read_boss_owner_property(entity, L"m_EntityActor") == pawn &&
                   try_read_int32_object_property(pawn, L"m_HitPoint").value_or(0) > 0;
        }
        auto initialize_dark_shooting(UObject* state) -> bool
        {
            if (!is_uobject_valid_fast(state) || !is_uobject_valid_fast(state->GetClassPrivate()) ||
                state->GetClassPrivate()->GetNamePrivate() != dark_shooting_name ||
                state->GetClassPrivate()->GetPathName() != data_master_dark_shooting_class ||
                state->HasAnyFlags(
                    static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)))
            {
                return true;
            }
            auto* pawn = read_boss_owner_property(state, L"MyCharPawn");
            // PostInitProperties precedes the action's gameplay ctor. Its pawn
            // property may not be assigned yet, but the queue's owning pawn is
            // already present in the bounded Outer chain.
            if (!is_uobject_valid_fast(pawn))
            {
                auto* outer = state->GetOuterPrivate();
                for (size_t depth = 0; depth < 8 && is_uobject_valid_fast(outer); ++depth)
                {
                    if (is_uobject_valid_fast(outer->GetClassPrivate()) &&
                        outer->GetClassPrivate()->GetPathName() == data_master_xehanort_class)
                    {
                        pawn = outer;
                        break;
                    }
                    outer = outer->GetOuterPrivate();
                }
            }
            if (!is_uobject_valid_fast(pawn))
            {
                return false;
            }
            std::optional<MasterArenaCenter> center;
            {
                std::lock_guard lock{owners_mutex};
                for (const auto& owner : owners)
                {
                    if (owner.pawn.Get() == pawn && live_owner(owner))
                    {
                        center = owner.center;
                    }
                }
            }
            return center && translate(state, *center, L"m_WarpPos", pawn);
        }

        void dark_shooting_post_init(UObject* state)
        {
            if (const auto original =
                    original_dark_shooting_post_init.load(std::memory_order_acquire))
            {
                original(state);
            }
            (void)initialize_dark_shooting(state);
        }

        auto install_dark_shooting_post_init() -> bool
        {
            auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresAttack3_e_ex367_DarkShooting");
            auto* defaults =
                is_uobject_valid_fast(klass) ? klass->GetClassDefaultObject() : nullptr;
            const auto slot = UObject::VTableLayoutMap.find(L"PostInitProperties");
            if (!is_uobject_valid_fast(defaults) || slot == UObject::VTableLayoutMap.end() ||
                slot->second == 0 || slot->second > 0x400 || slot->second % sizeof(void*) != 0)
            {
                return false;
            }
            auto** vtable = *reinterpret_cast<void***>(defaults);
            if (!vtable)
            {
                return false;
            }
            auto** cell = &vtable[slot->second / sizeof(void*)];
            const auto target = reinterpret_cast<uintptr_t>(*cell);
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return false;
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            MEMORY_BASIC_INFORMATION region{};
            if (nt->Signature != IMAGE_NT_SIGNATURE || target < base ||
                target - base >= nt->OptionalHeader.SizeOfImage ||
                !VirtualQuery(*cell, &region, sizeof(region)) ||
                !(region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                    PAGE_EXECUTE_WRITECOPY)))
            {
                return false;
            }
            auto hook = std::make_unique<RuntimeBossHpLimitOverride>();
            hook->slot = cell;
            hook->original = *cell;
            hook->replacement = reinterpret_cast<void*>(&dark_shooting_post_init);
            PLH::MemAccessor memory{};
            PLH::MemoryProtector protection{reinterpret_cast<uint64_t>(cell), sizeof(*cell),
                                            PLH::ProtFlag::R | PLH::ProtFlag::W, memory};
            if (!protection.isGood())
            {
                return false;
            }
            original_dark_shooting_post_init.store(reinterpret_cast<PostInitProperties>(*cell),
                                                   std::memory_order_release);
            *cell = hook->replacement;
            dark_shooting_post_init_override = std::move(hook);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Master Xehanort Dark Shooting initialization installed "
                    "vtable_offset=0x{:X} scope=exact_action_instance polling=false\n"),
                slot->second);
            return true;
        }
    } // namespace

    void enemy_remap_detail::prepare_master_xehanort_action(UObject* pawn)
    {
        if (!configured.load(std::memory_order_acquire) ||
            !has_owners.load(std::memory_order_acquire) || !pawn || !pawn->GetClassPrivate() ||
            pawn->GetClassPrivate()->GetNamePrivate() != master_name)
        {
            return;
        }
        auto* component = read_boss_owner_property(pawn, L"MyStateComp");
        auto* state = read_boss_owner_property(component, L"m_pCurrentState");
        if (is_uobject_valid_fast(state))
        {
            (void)initialize_dark_shooting(state);
        }
    }

    void enemy_remap_detail::prepare_master_xehanort_center(UObject* pawn)
    {
        // Master Xehanort can enter Idle directly without an Appear state.
        // Reuse the existing ctor-state event, with an allocation-free class
        // filter and an already-initialized weak-owner check.
        if (!configured.load(std::memory_order_acquire) || !pawn || !pawn->GetClassPrivate() ||
            pawn->GetClassPrivate()->GetNamePrivate() != master_name)
        {
            return;
        }
        activate_master_query_contexts();
        {
            std::lock_guard lock{owners_mutex};
            if (std::any_of(owners.begin(), owners.end(),
                            [pawn](const auto& owner)
                            {
                                return owner.pawn.Get() == pawn && live_owner(owner);
                            }))
            {
                return;
            }
        }
        const auto binding = resolve_and_bind_runtime_boss_pawn(pawn);
        if (binding.level_entity)
        {
            initialize_master_xehanort_center(pawn, binding.level_entity);
        }
    }

    void enemy_remap_detail::initialize_master_xehanort_center(UObject* pawn, UObject* entity)
    {
        if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(entity) ||
            !is_uobject_valid_fast(pawn->GetClassPrivate()))
        {
            return;
        }
        const auto target = pawn->GetClassPrivate()->GetPathName();
        if (target != data_master_xehanort_class)
        {
            return;
        }
        const auto source = tracked_boss_source_class(entity->GetPathName());
        if (!source || !needs_master_arena_center(*source, target))
        {
            return;
        }
        auto* world = owning_world(pawn);
        const auto center = arena_center(pawn);
        if (!is_uobject_valid_fast(world) || !center || !translate(pawn, *center))
        {
            Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Master Xehanort center unavailable "
                                              "pawn={} polling=false\n"),
                                          pawn->GetPathName());
            return;
        }
        ArenaOwner owner{make_uobject_weak(pawn), make_uobject_weak(entity),
                         make_uobject_weak(world), *center};
        if (!live_owner(owner))
        {
            return;
        }
        std::lock_guard lock{owners_mutex};
        std::erase_if(owners,
                      [&](const auto& entry)
                      {
                          return !live_owner(entry) || entry.pawn.Get() == pawn;
                      });
        if (owners.size() >= max_owners)
        {
            return;
        }
        owners.push_back(owner);
        has_owners.store(true, std::memory_order_release);
    }

    static bool initialize_master_ascension_center(UObject* actor)
    {
        // Runs on the game thread after the creation callback's constructor
        // has returned. Resolve only this weak actor and its recorded owners.
        if (!has_owners.load(std::memory_order_acquire))
        {
            return false;
        }
        if (!actor || !actor->GetClassPrivate() ||
            actor->GetClassPrivate()->GetNamePrivate() != ascension_name ||
            actor->HasAnyFlags(
                static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)))
        {
            return true;
        }
        if (!is_uobject_valid_fast(actor) ||
            actor->GetClassPrivate()->GetPathName() != data_master_ascension_class)
        {
            return true;
        }
        auto* world = owning_world(actor);
        if (!is_uobject_valid_fast(world))
        {
            return false;
        }
        std::optional<MasterArenaCenter> center;
        bool ambiguous = false;
        {
            std::lock_guard lock{owners_mutex};
            std::erase_if(owners,
                          [](const auto& entry)
                          {
                              return !live_owner(entry);
                          });
            for (const auto& owner : owners)
            {
                if (owner.world.Get() != world)
                {
                    continue;
                }
                if (read_boss_owner_property(owner.pawn.Get(), L"m_AscensionObject") == actor)
                {
                    center = owner.center;
                    ambiguous = false;
                    break;
                }
                // Prefer the explicit owning-pawn link. If it is not assigned,
                // only accept one common encounter center in this exact world.
                if (center && *center != owner.center)
                {
                    ambiguous = true;
                }
                center = owner.center;
            }
        }
        return center && !ambiguous && translate(actor, *center);
    }

    void notify_master_ascension_created(UObject* actor)
    {
        if (!configured.load(std::memory_order_acquire) || !actor || !actor->GetClassPrivate() ||
            (actor->GetClassPrivate()->GetNamePrivate() != ascension_name &&
             actor->GetClassPrivate()->GetNamePrivate() != dark_shooting_name) ||
            actor->HasAnyFlags(
                static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_ArchetypeObject)))
        {
            return;
        }
        auto weak = make_uobject_weak(actor);
        if (weak.Get() != actor)
        {
            return;
        }
        std::lock_guard lock{pending_mutex};
        if (pending.size() >= max_pending)
        {
            return;
        }
        pending.push_back({weak,
                           Clock::now() + std::chrono::seconds(2),
                           {},
                           actor->GetClassPrivate()->GetNamePrivate() == dark_shooting_name});
        pending_count.store(pending.size(), std::memory_order_release);
    }

    void process_pending_master_ascension_centers()
    {
        if (!pending_count.load(std::memory_order_acquire))
        {
            return;
        }
        std::vector<PendingAscension> work;
        {
            std::lock_guard lock{pending_mutex};
            work.swap(pending);
            pending_count.store(0, std::memory_order_release);
        }
        const auto now = Clock::now();
        std::erase_if(work,
                      [now](auto& entry)
                      {
                          auto* actor = entry.actor.Get();
                          if (!is_uobject_valid_fast(actor))
                          {
                              return true;
                          }
                          if (now < entry.next_attempt)
                          {
                              return false;
                          }
                          if (entry.action ? initialize_dark_shooting(actor)
                                           : initialize_master_ascension_center(actor))
                          {
                              return true;
                          }
                          if (now >= entry.deadline)
                          {
                              Output::send<LogLevel::Warning>(
                                  STR("[KH3APProbeCpp] Master Xehanort Ascension center "
                                      "not_initialized actor={} reason=owner_or_layout_unavailable "
                                      "bounded_retry_expired=true\n"),
                                  actor->GetPathName());
                              return true;
                          }
                          entry.next_attempt = now + std::chrono::milliseconds(100);
                          return false;
                      });
        if (!work.empty())
        {
            std::lock_guard lock{pending_mutex};
            for (const auto& entry : work)
            {
                if (pending.size() < max_pending)
                {
                    pending.push_back(entry);
                }
            }
            pending_count.store(pending.size(), std::memory_order_release);
        }
    }

    void enemy_remap_detail::install_master_xehanort_center_support()
    {
        const auto& replacements = s_runtime_enemy_remap.boss.boss_class_replacements;
        if (!std::any_of(replacements.begin(), replacements.end(),
                         [](const auto& pair)
                         {
                             return needs_master_arena_center(pair.first, pair.second);
                         }))
        {
            return;
        }
        ascension_name = FName{L"e_ex367_AscensionPawn_C", FNAME_Add};
        master_name = FName{L"e_ex367_Pawn_C", FNAME_Add};
        dark_shooting_name = FName{L"e_ex367_Attack3_DarkShooting_TD_C", FNAME_Add};
        if (!install_dark_shooting_post_init())
        {
            Output::send<LogLevel::Error>(
                STR("[KH3APProbeCpp] Master Xehanort Dark Shooting initialization unavailable "
                    "reason=native_class_or_vtable_layout\n"));
        }
        configured.store(true, std::memory_order_release);
        configure_master_query_contexts();
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Master Xehanort center support installed "
                "source=existing_uobject_create pending_retry_seconds=2 idle_polling=false\n"));
    }
} // namespace kh3ap
