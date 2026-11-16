#include "kh3ap/features/enemies/titan_arena.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/events/event_work.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/titan_object_registry.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <Windows.h>

namespace kh3ap
{
    namespace
    {
        constexpr auto titan_obstacle_readiness_retry = std::chrono::milliseconds(100);
        constexpr auto titan_obstacle_failure_retry = std::chrono::seconds(10);
    }

    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        std::mutex entity_mutex;
        std::array<std::wstring,2> targets;
        std::atomic<uint64_t> binding_revision{};
        uint64_t seen_binding{~uint64_t{0}}, seen_objects{~uint64_t{0}};
        RuntimeEventWork work;
        bool was_enabled{};
        struct Original { FWeakObjectPtr actor; bool collision{}, hidden{}; };
        std::vector<Original> originals;
        constexpr std::wstring_view entities[]{
            L"/Game/Levels/he/he_05/umap/he_05_enemy_01.he_05_enemy_01:PersistentLevel.e_he902_Pawn1317",
            L"/Game/Levels/he/he_05/umap/he_05_enemy_01.he_05_enemy_01:PersistentLevel.e_he904_Pawn1045",
        };
        constexpr std::wstring_view sources[]{
            L"/Game/Blueprints/Enemy/e_he902/e_he902_Pawn.e_he902_Pawn_C",
            L"/Game/Blueprints/Enemy/e_he904/e_he904_Pawn.e_he904_Pawn_C",
        };

        bool has_titan_attack_gate_layout()
        {
            const auto* image = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
            if (!image) return false;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
            const auto build = native_build::identify(nt->FileHeader.TimeDateStamp,
                                                      nt->OptionalHeader.SizeOfImage);
            // The native Titan activation routine writes one byte at +0xcd5
            // on both builds. Its surrounding code also changes Olympus AI state.
            constexpr uint8_t activation[]{0x40, 0x53, 0x48, 0x83, 0xec, 0x20,
                                           0x48, 0x8b, 0xd9, 0xc6, 0x81, 0xd5,
                                           0x0c, 0x00, 0x00, 0x01};
            switch (build)
            {
            case native_build::Build::Egs:
                return std::memcmp(image + 0x8d9bc0, activation, sizeof(activation)) == 0;
            case native_build::Build::Steam:
                return std::memcmp(image + 0x723030, activation, sizeof(activation)) == 0;
            default:
                return false;
            }
        }

        bool bool_call(UObject* object, const wchar_t* name, const wchar_t* param, bool value)
        {
            auto* fn = object->GetFunctionByNameInChain(name);
            auto* p = fn ? find_function_param_by_name(fn, param) : nullptr;
            if (!p || fn->GetParmsSize() != 1 || p->GetOffset_Internal() != 0 ||
                    p->GetClass().GetName() != L"BoolProperty") throw std::runtime_error("titan_arena_bool_layout");
            uint8_t buffer = value;
            object->ProcessEvent(fn, &buffer);
            return param == std::wstring_view(L"ReturnValue") ? buffer != 0 : true;
        }
        bool hidden_value(UObject* actor)
        {
            auto* field = actor->GetPropertyByNameInChain(L"bHidden");
            if (!field || field->GetClass().GetName() != L"BoolProperty")
                throw std::runtime_error("titan_arena_hidden_layout");
            return static_cast<FBoolProperty*>(field)->GetPropertyValueInContainer(actor);
        }
        void restore()
        {
            for (const auto& original : originals)
            {
                auto* actor = original.actor.Get();
                if (!is_uobject_valid_fast(actor)) continue;
                bool_call(actor, L"SetActorEnableCollision", L"bNewActorEnableCollision", original.collision);
                bool_call(actor, L"SetActorHiddenInGame", L"bNewHidden", original.hidden);
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Titan arena obstacle restored actor={} collision_enabled={} hidden={}\n"),
                    actor->GetPathName(), bool_call(actor,L"GetActorEnableCollision",L"ReturnValue",true), hidden_value(actor));
            }
            originals.clear();
        }
    }

    void configure_titan_arena_replacement(std::wstring_view source, std::wstring_view target)
    {
        for (size_t i=0;i<2;++i)
            if (source==sources[i]) titan_objects::watch_pawn_class(i,target);
    }

    void track_titan_arena_entity(UObject* entity, std::wstring_view source, std::wstring_view target)
    {
        if (!is_uobject_valid_fast(entity)) return;
        const auto path=entity->GetPathName();
        for (size_t i=0; i<2; ++i)
        {
            if (path!=entities[i] || source!=sources[i]) continue;
            std::lock_guard lock{entity_mutex};
            targets[i]=target;
            binding_revision.fetch_add(1,std::memory_order_release);
        }
    }

    void notify_titan_arena_binding(std::wstring_view path)
    {
        for (const auto entity : entities)
            if (path==entity) { binding_revision.fetch_add(1,std::memory_order_release); return; }
    }

    void install_replacement_titan_attack_support()
    {
        using namespace enemy_remap_detail;
        const auto& remaps = s_runtime_enemy_remap.boss.boss_class_replacements;
        if (std::none_of(remaps.begin(), remaps.end(), [](const auto& remap) {
                return remap.second == sources[0] || remap.second == sources[1];
            }) || !has_titan_attack_gate_layout())
        {
            return;
        }

        static const FName ice_class{L"e_he902_Pawn_C", FNAME_Add};
        static const FName lava_class{L"e_he904_Pawn_C", FNAME_Add};
        static const FName olympus_enemy_package{L"he_05_enemy_01", FNAME_Add};
        UObjectGlobals::RegisterHook(
                STR("/Script/TresGame.TresEnemyPawnBase_e_he90y:CanAttack"),
                [](UnrealScriptFunctionCallableContext& context, void*) {
                    auto* pawn = context.Context;
                    if (!s_runtime_enemy_remap.boss.boss_enabled.load() ||
                        !is_uobject_valid_fast(pawn))
                    {
                        return;
                    }

                    const auto* pawn_class = pawn->GetClassPrivate();
                    if (!is_uobject_valid_fast(pawn_class))
                    {
                        return;
                    }
                    const auto class_name = pawn_class->GetNamePrivate();
                    if (!class_name.Equals(ice_class) && !class_name.Equals(lava_class))
                    {
                        return;
                    }

                    constexpr auto attack_gate_offset = 0xcd5;
                    auto& attack_enabled = *(reinterpret_cast<uint8_t*>(pawn) + attack_gate_offset);
                    if (attack_enabled != 0) return;

                    const auto* level = pawn->GetOuterPrivate();
                    const auto* package = is_uobject_valid_fast(level) ? level->GetOuterPrivate() : nullptr;
                    if (!is_uobject_valid_fast(package) ||
                        package->GetNamePrivate().Equals(olympus_enemy_package))
                    {
                        return;
                    }

                    const auto binding = resolve_and_bind_runtime_boss_pawn(pawn);
                    if (!binding.level_entity ||
                        binding.level_entity_path.starts_with(L"/Game/Levels/he/he_05/"))
                    {
                        return;
                    }

                    // The BT asks at the first attack decision, after the pawn
                    // is live. Supply the activation the paired encounter owns.
                    attack_enabled = 1;
                    Output::send<LogLevel::Normal>(
                            STR("[KH3APProbeCpp] Replacement Titan attack gate opened entity={} pawn={}\n"),
                            binding.level_entity_path, pawn->GetPathName());
                },
                [](UnrealScriptFunctionCallableContext&, void*) {},
                nullptr);
    }

    void process_titan_arena_replacement(bool enabled)
    {
        titan_objects::initialize();
        const auto objects=titan_objects::generation();
        const auto binding=binding_revision.load(std::memory_order_acquire);
        if (objects!=seen_objects || binding!=seen_binding || enabled!=was_enabled) work.request();
        seen_objects=objects;seen_binding=binding;was_enabled=enabled;
        const auto now=Clock::now();
        if (!work.take(now)) return;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeHotPathProfileScope profile{L"enemies:titan_obstacles"};
#endif
        try
        {
            if (!enabled) { if (!originals.empty()) restore(); return; }
            const auto snapshot=titan_objects::snapshot();

            std::array<UObject*,2> tracked_entities{};
            std::array<UObject*,3> obstacles{};
            for (auto* actor : snapshot.objects)
            {
                const auto path=actor->GetPathName();
                for (size_t i=0;i<tracked_entities.size();++i)
                    if (path==entities[i]) tracked_entities[i]=actor;
                for (size_t i=0;i<obstacles.size();++i)
                    if (path==titan_arena_obstacles[i]) obstacles[i]=actor;
            }
            bool unfinished=snapshot.unfinished;
            for (auto* candidate : snapshot.pawns)
            {
                bool same_level=false, bound=false;
                for (auto* entity : tracked_entities)
                    if (entity && entity->GetOuterPrivate()==candidate->GetOuterPrivate())
                    {
                        same_level=true;
                        bound |= try_read_reflected_object_property_value(entity,L"m_EntityActor")==candidate;
                    }
                // Constructor notification precedes FinishSpawning's owner write.
                // Retry only an actually constructed, still-unbound pawn. Merely
                // loading an inactive arena never starts a spawn readiness poll.
                unfinished |= same_level && !bound;
            }
            work.finish(now,!unfinished,titan_obstacle_readiness_retry);
            bool replacement_bound = false;
            bool awaiting_binding = false;
            bool unexpected_binding = false;
            {
                std::lock_guard lock{entity_mutex};
                for (size_t i=0;i<tracked_entities.size();++i)
                {
                    auto* entity=tracked_entities[i];
                    if (!entity || targets[i].empty()) continue;
                    auto* pawn=try_read_reflected_object_property_value(entity,L"m_EntityActor");
                    if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(pawn->GetClassPrivate()))
                    {
                        awaiting_binding = true;
                        continue;
                    }
                    const auto actual = pawn->GetClassPrivate()->GetPathName();
                    if (actual == targets[i] && !actual.starts_with(L"/Game/Blueprints/Enemy/e_he90"))
                    {
                        replacement_bound = true;
                        break;
                    }
                    unexpected_binding = true;
                }
            }
            // Keep removal during Retry's no-pawn interval. The ownership-bind
            // event wakes us when its replacement pawns have been assigned.
            if (!replacement_bound)
            {
                if (unexpected_binding && !awaiting_binding && !originals.empty()) restore();
                return;
            }
            // Wait for all three streamed actors before changing any geometry.
            for (auto* actor : obstacles) if (!actor) return;
            // Retry may replace the level actors. Never use the previous
            // encounter's saved values as the new actors' restoration record.
            if (!originals.empty())
            {
                bool same = originals.size() == obstacles.size();
                for (size_t i = 0; same && i < obstacles.size(); ++i)
                    same = originals[i].actor.Get() == obstacles[i];
                if (!same) restore();
            }
            if (originals.empty())
            {
                std::vector<Original> captured;
                for (auto* actor : obstacles)
                {
                    Original original;
                    original.actor=make_uobject_weak(actor);
                    if (original.actor.Get()!=actor) throw std::runtime_error("titan_arena_weak_unavailable");
                    original.collision = bool_call(actor,L"GetActorEnableCollision",L"ReturnValue",true);
                    original.hidden = hidden_value(actor);
                    captured.push_back(original);
                }
                originals=std::move(captured);
            }
            bool complete = true;
            for (size_t i = 0; i < obstacles.size(); ++i)
            {
                auto* actor = obstacles[i];
                const bool before=bool_call(actor,L"GetActorEnableCollision",L"ReturnValue",true);
                const bool was_hidden=hidden_value(actor);
                const bool changed=!before || bool_call(actor,L"SetActorEnableCollision",L"bNewActorEnableCollision",false);
                bool hidden = true;
                if (i == 2)
                {
                    hidden = was_hidden || bool_call(actor, L"SetActorHiddenInGame", L"bNewHidden", true);
                    hidden = hidden && hidden_value(actor);
                }
                const bool collision = bool_call(actor, L"GetActorEnableCollision", L"ReturnValue", true);
                complete &= changed && !collision && hidden;
                if (before || (i==2 && !was_hidden))
                    Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Titan arena obstacle actor={} collision_enabled={} hidden_verified={} titan_replacement_bound=true\n"),
                        actor->GetPathName(), collision, hidden);
            }
            if (!complete) throw std::runtime_error("titan_arena_readback");
        }
        catch (...) { work.finish(now,false,titan_obstacle_failure_retry); Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Titan arena obstacle update failed\n")); }
    }
}
