#include <array>
#include "kh3ap/features/ingredients/ingredient_tracker.h"
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/enemies/titan_arena.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <iterator>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_spawn_point_results = 128;
    }

    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    void RuntimeEnemyClearService::Impl::inspect_scene(UObject* pawn)
    {
        // On-demand evidence only; this service is diagnostics-only.
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        std::ostringstream counts;
        counts << '{';
        for (int index = ingredient_ids::first_item; index <= ingredient_ids::last_item; ++index)
        {
            const auto token =
                L"FST_ITEM" + std::wstring(index < 10 ? L"0" : L"") + std::to_wstring(index);
            Call query(library, L"BP_TresGetBackyardItemNum");
            if (!write_object_function_param(query.function, query.params.data(),
                                             L"WorldContextObject", pawn) ||
                !write_name_or_string_function_param(query.field(L"InItemKeyName"),
                                                     query.params.data(), token))
            {
                throw std::runtime_error("ingredient_inventory_query_layout");
            }
            query.invoke();
            auto count =
                read_function_int_param(query.function, query.params.data(), L"ReturnValue");
            if (!count || *count < 0)
            {
                throw std::runtime_error("ingredient_inventory_query_failed");
            }
            if (index > 1)
            {
                counts << ',';
            }
            counts << '"' << json_escape(token) << "\":" << *count;
        }
        counts << '}';
        ingredient_counts = counts.str();
        struct Entry
        {
            UObject* actor;
            std::wstring kind;
            std::array<float, 3> pos;
            double distance;
        };
        std::vector<Entry> entries;
        for (const auto& [class_name, kind] :
             std::array{std::pair{L"/Script/TresGame.TresTreasureBox", L"chest"},
                        std::pair{L"/Script/TresGame.TresVehiclePawnSpecialShip", L"ship"},
                        std::pair{L"/Script/TresGame.TresGimmickActor", L"ingredient_gimmick"},
                        std::pair{L"/Script/TresGame.TresMapJumpTriggerBox", L"map_trigger"},
                        std::pair{L"/Script/TresGame.TresMapJumpDirectionalTriggerBox",
                                  L"directional_map_trigger"},
                        std::pair{L"/Script/TresGame.TresMapJumpVolume", L"map_volume"}})
        {
            auto* klass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, class_name);
            for (auto* actor : actors_of_class(pawn, klass))
            {
                if (!is_uobject_valid_fast(actor))
                {
                    continue;
                }
                if (std::wstring_view(kind) == L"ingredient_gimmick")
                {
                    const auto food = property_name(actor, L"m_FoodstuffKeyName");
                    if (food.empty() || food == L"None")
                    {
                        continue;
                    }
                }
                const auto pos = position(actor);
                double distance = 0;
                for (size_t i = 0; i < 3; ++i)
                {
                    distance += std::pow(pos[i] - position_before[i], 2.0);
                }
                entries.push_back({actor, kind, pos, std::sqrt(distance)});
                if (entries.size() > max_inspected_scene_objects)
                {
                    throw std::runtime_error("scene_object_limit_exceeded");
                }
            }
        }
        // Inspect current-world prize actors without collecting or moving them.
        auto* actor_class =
            UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/Engine.Actor");
        for (auto* actor : actors_of_class(pawn, actor_class))
        {
            if (!is_uobject_valid_fast(actor) || !is_uobject_valid_fast(actor->GetClassPrivate()))
            {
                continue;
            }
            const auto name = actor->GetClassPrivate()->GetName();
            const auto path = actor->GetPathName();
            const bool titan_obstacle =
                std::any_of(std::begin(titan_arena_obstacles), std::end(titan_arena_obstacles),
                            [&](auto expected)
                            {
                                return path == expected;
                            });
            if (!titan_obstacle && (name.find(L"Prize") == std::wstring::npos ||
                                    name.find(L"Gimmick") != std::wstring::npos))
            {
                continue;
            }
            if (std::any_of(entries.begin(), entries.end(),
                            [&](const auto& entry)
                            {
                                return entry.actor == actor;
                            }))
            {
                continue;
            }
            const auto pos = position(actor);
            double distance = 0;
            for (size_t i = 0; i < 3; ++i)
            {
                distance += std::pow(pos[i] - position_before[i], 2.0);
            }
            entries.push_back(
                {actor, titan_obstacle ? L"titan_obstacle" : L"prize", pos, std::sqrt(distance)});
            if (entries.size() > max_inspected_scene_objects)
            {
                throw std::runtime_error("scene_object_limit_exceeded");
            }
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b)
                  {
                      return a.distance < b.distance;
                  });
        std::ostringstream out;
        out << std::boolalpha << '[';
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            if (i)
            {
                out << ',';
            }
            out << "{\"actor\":\"" << json_escape(e.actor->GetPathName()) << "\",\"kind\":\""
                << json_escape(e.kind) << "\",\"treasure_id\":\""
                << json_escape(property_name(e.actor, L"m_TreasureID")) << "\",\"foodstuff_key\":\""
                << json_escape(property_name(e.actor, L"m_FoodstuffKeyName")) << "\",\"map\":\""
                << json_escape(property_name(e.actor, L"m_MapName")) << "\",\"tag\":\""
                << json_escape(property_name(e.actor, L"m_TargetTag"))
                << "\",\"hidden\":" << property_bool(e.actor, L"bHidden", false)
                << ",\"active\":" << property_bool(e.actor, L"m_bIsActive", true)
                << ",\"collision_enabled\":" << boolean(e.actor, L"GetActorEnableCollision")
                << ",\"distance\":" << e.distance << ",\"position\":";
            json_position(out, e.pos);
            if (e.kind == L"ingredient_gimmick")
            {
                Call bounds(e.actor, L"GetActorBounds");
                if (!write_bool_function_param(bounds.field(L"bOnlyCollidingComponents"),
                                               bounds.params.data(), true))
                {
                    throw std::runtime_error("ingredient_bounds_layout");
                }
                bounds.invoke();
                for (const auto* name : {L"Origin", L"BoxExtent"})
                {
                    auto* field = bounds.field(name);
                    if (field->GetClass().GetName() != L"StructProperty" ||
                        field->GetElementSize() != sizeof(std::array<float, 3>))
                    {
                        throw std::runtime_error("ingredient_bounds_vector_layout");
                    }
                    std::array<float, 3> value{};
                    std::memcpy(value.data(),
                                field->ContainerPtrToValuePtr<void>(bounds.params.data()),
                                sizeof(std::array<float, 3>));
                    out << (std::wstring_view(name) == L"Origin" ? ",\"bounds_origin\":"
                                                                 : ",\"bounds_extent\":");
                    json_position(out, value);
                }
            }
            if (e.kind == L"ship")
            {
                auto* component_class = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, L"/Script/Engine.SceneComponent");
                out << ",\"traversal_components\":[";
                bool first = true;
                for (auto* component : owned_components(e.actor, component_class))
                {
                    const auto name = component->GetName();
                    if (name.find(L"Reactor") == std::wstring::npos &&
                        name.find(L"Hop") == std::wstring::npos &&
                        name.find(L"Pole") == std::wstring::npos)
                    {
                        continue;
                    }
                    Call location(component, L"K2_GetComponentLocation");
                    auto* result = location.field(L"ReturnValue");
                    if (result->GetElementSize() != sizeof(std::array<float, 3>))
                    {
                        throw std::runtime_error("traversal_component_location_layout");
                    }
                    location.invoke();
                    std::array<float, 3> component_position{};
                    std::memcpy(component_position.data(),
                                result->ContainerPtrToValuePtr<void>(location.params.data()),
                                sizeof(component_position));
                    if (!first)
                    {
                        out << ',';
                    }
                    first = false;
                    out << "{\"component\":\"" << json_escape(component->GetPathName())
                        << "\",\"position\":";
                    json_position(out, component_position);
                    out << '}';
                }
                out << ']';
            }
            if (e.kind == L"chest")
            {
                auto* mesh = try_read_reflected_object_property_value(e.actor, L"MyMesh");
                auto* asset = try_read_reflected_object_property_value(mesh, L"SkeletalMesh");
                auto* material = material_zero(mesh);
                Call anim(mesh, L"GetAnimInstance");
                anim.invoke();
                auto* instance = try_read_function_object_param(anim.function, anim.params.data(),
                                                                L"ReturnValue");
                auto* reactor = try_read_reflected_object_property_value(e.actor, L"MyReactor");
                out << ",\"mesh\":\""
                    << json_escape(is_uobject_valid_fast(asset) ? asset->GetPathName() : L"")
                    << "\",\"anim_instance\":\""
                    << json_escape(is_uobject_valid_fast(instance) ? instance->GetPathName() : L"")
                    << "\",\"material\":\""
                    << json_escape(is_uobject_valid_fast(material) ? material->GetPathName() : L"")
                    << "\",\"vanilla_recorded\":" << reconcile_chest_record(e.actor)
                    << ",\"reactor_active\":"
                    << (is_uobject_valid_fast(reactor) &&
                        boolean(reactor, L"BP_IsGimmickComponentActive"))
                    << ",\"object_index\":" << e.actor->GetInternalIndex();
                if (is_uobject_valid_fast(reactor))
                {
                    bool remapped_callback{};
                    for (const auto& binding : chest_delegate(reactor)->InvocationList)
                    {
                        if (binding.FunctionName.ToString() == L"OnChestChecked" &&
                            is_uobject_valid_fast(binding.Object.Get()))
                        {
                            remapped_callback = true;
                        }
                    }
                    out << ",\"reactor_command\":" << static_cast<int>(reactor_command(reactor))
                        << ",\"remapped_callback_bound\":" << remapped_callback;
                }
            }
            out << '}';
        }
        out << ']';
        scene_objects = out.str();
    }

    void RuntimeEnemyClearService::Impl::enumerate(UObject* pawn)
    {
        const auto players = actors_of_class(pawn, pawn->GetClassPrivate());
        if (std::find(players.begin(), players.end(), pawn) == players.end())
        {
            throw std::runtime_error("actor_enumeration_failed_sora_control");
        }
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresEnemyPawnBase");
        if (!is_uobject_valid_fast(klass))
        {
            throw std::runtime_error("enemy_class_unavailable");
        }
        const auto team = integer(pawn, L"GetTeamNo");
        const auto actors = actors_of_class(pawn, klass);
        if (actors.size() > max_targets)
        {
            throw std::runtime_error("enemy_limit_exceeded");
        }
        for (auto* actor : actors)
        {
            // The engine enumeration already restricts to Sora's world,
            // including streamed levels. UE4SS AActor::GetWorld walks
            // outers and returns a streamed package's template world.
            if (!is_uobject_valid_fast(actor) || actor == pawn)
            {
                continue;
            }
            const auto enemy_team = integer(actor, L"GetTeamNo");
            if (enemy_team == team)
            {
                ++skipped_friendly;
                continue;
            }
            const auto state = integer(actor, L"GetStateID");
            if (state == player_state_ids::die || state == player_state_ids::vanish_die)
            {
                continue;
            }
            Target t;
            t.actor = actor;
            if (t.actor.Get() != actor)
            {
                throw std::runtime_error("enemy_weak_identity_unavailable");
            }
            t.name = actor->GetPathName();
            t.class_name = actor->GetClassPrivate()->GetPathName();
            t.hp_before = t.hp_after = integer(actor, L"GetHitPoint");
            t.max_hp = integer(actor, L"GetMaxHitPoint");
            t.state_before = t.state_after = state;
            t.team = enemy_team;
            t.location = position(actor);
            t.invincible = boolean(actor, L"IsConditionInvincible");
            t.hp_floor = boolean(actor, L"IsConditionHpLimitOne");
            t.no_damage = boolean(actor, L"IsConditionNoHpDamage");
            auto* entity = try_read_reflected_object_property_value(actor, L"m_LevelEntity");
            if (is_uobject_valid_fast(entity))
            {
                t.entity = entity;
                t.entity_name = entity->GetPathName();
                t.kills_before = t.kills_after = property_int(entity, L"m_KillCount");
            }
            targets.push_back(std::move(t));
        }
        // Count shared entities once; observing a large clear must not
        // compare every target with every other target on every frame.
        std::unordered_map<std::wstring, size_t> entity_counts;
        for (const auto& t : targets)
        {
            ++entity_counts[t.entity_name];
        }
        for (auto& t : targets)
        {
            t.entity_targets = entity_counts[t.entity_name];
        }
        if (command != L"inspect")
        {
            return;
        }
        auto* entity_class = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresLevelEntity");
        struct Spawn
        {
            std::wstring name, path;
            std::array<float, 3> location;
            int32_t spawned, killed;
            double distance;
        };
        std::vector<Spawn> spawns;
        for (auto* entity : actors_of_class(pawn, entity_class))
        {
            if (!is_uobject_valid_fast(entity))
            {
                continue;
            }
            auto* p = entity->GetPropertyByNameInChain(L"m_ActorClassPath");
            if (!p || p->GetClass().GetName() != L"StructProperty" ||
                p->GetElementSize() < sizeof(FString) ||
                static_cast<FStructProperty*>(p)->GetStruct()->GetName() != L"StringAssetReference")
            {
                continue;
            }
            const auto* path = p->ContainerPtrToValuePtr<FString>(entity);
            const auto* chars = path->GetCharArray();
            if (!chars)
            {
                continue;
            }
            const std::wstring name{chars};
            if (name.find(L"/Blueprints/Enemy/") == std::wstring::npos)
            {
                continue;
            }
            const auto loc = position(entity);
            double distance = 0;
            for (size_t i = 0; i < 3; ++i)
            {
                distance += std::pow(loc[i] - position_before[i], 2.0);
            }
            spawns.push_back({entity->GetPathName(), name, loc,
                              property_int(entity, L"m_SpawnCount"),
                              property_int(entity, L"m_KillCount"), std::sqrt(distance)});
        }
        std::sort(spawns.begin(), spawns.end(),
                  [](const auto& a, const auto& b)
                  {
                      return a.distance < b.distance;
                  });
        std::ostringstream out;
        out << '[';
        for (size_t i = 0; i < std::min<size_t>(spawns.size(), max_spawn_point_results); ++i)
        {
            const auto& s = spawns[i];
            if (i)
            {
                out << ',';
            }
            out << "{\"entity\":\"" << json_escape(s.name) << "\",\"class\":\""
                << json_escape(s.path) << "\",\"spawned\":" << s.spawned
                << ",\"killed\":" << s.killed << ",\"distance\":" << s.distance << ",\"position\":";
            json_position(out, s.location);
            out << '}';
        }
        out << ']';
        spawn_points = out.str();
    }

    void RuntimeEnemyClearService::Impl::sample()
    {
        for (auto& t : targets)
        {
            auto* actor = t.actor.Get();
            t.vanished = !is_uobject_valid_fast(actor);
            if (!t.vanished)
            {
                t.hp_after = integer(actor, L"GetHitPoint");
                t.location = position(actor);
                t.state_after = integer(actor, L"GetStateID");
                if (t.state_after == player_state_ids::die ||
                    t.state_after == player_state_ids::vanish_die)
                {
                    t.death_observed = true;
                    t.proof = L"native_death_state";
                }
            }
            auto* entity = t.entity.Get();
            if (is_uobject_valid_fast(entity))
            {
                t.kills_after = property_int(entity, L"m_KillCount");
            }
            // Disappearance alone is never defeat proof. A shared entity
            // must account for every selected instance before using its counter.
            if (t.vanished && !t.entity_name.empty() && t.kills_before >= 0 &&
                static_cast<int64_t>(t.kills_after) - t.kills_before >=
                    static_cast<int64_t>(t.entity_targets))
            {
                t.proof = L"entity_kill_counter";
            }
        }
    }

    void RuntimeEnemyClearService::Impl::accept_inspect_scene(UObject* pawn,
                                                              const std::string& text)
    {
        inspect_scene(pawn);
        finish(L"completed", L"scene_snapshot");
        return;
    }

} // namespace kh3ap
