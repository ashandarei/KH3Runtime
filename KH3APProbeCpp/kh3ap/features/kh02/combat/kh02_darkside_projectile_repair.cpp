#include "kh02_darkside_projectile_repair.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh02_darkside_reflection_policy.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <nlohmann/json.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto forest_world_path = L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest";
        constexpr auto projectile_class_path =
            L"/Game/Zlueprints/Enemy/e_dw407/e_dw407_DarkHomingProjectile.e_dw407_DarkHomingProjectile_C";
        constexpr auto boss_class_path = L"/Game/Zlueprints/Enemy/e_dw407b/e_dw407b_Pawn.e_dw407b_Pawn_C";
        constexpr auto first_boss_class_path = L"/Game/Zlueprints/Enemy/e_dw407/e_dw407_Pawn.e_dw407_Pawn_C";
        constexpr auto source_actor_prefix = L"/Game/Zevels/dw/dw_08/umap/dw_08_enemy.dw_08_enemy";
        constexpr std::size_t maximum_tracked_projectiles = 128;
        constexpr std::int32_t maximum_ignored_actors = 16;
        constexpr std::size_t actor_team_offset = 0x514;
        constexpr std::size_t projectile_collision_offset = 0x5a8;
        constexpr std::size_t projectile_state_offset = 0x6c0;
        constexpr std::size_t projectile_homing_target_offset = 0x818;
        constexpr std::size_t collision_team_offset = 0x868;
        constexpr std::size_t collision_ignored_actors_offset = 0x870;
        constexpr std::size_t set_team_virtual_offset = 0xd88;

        template <class T> auto native_read(const void* object, std::size_t offset, T& result) noexcept -> bool
        {
            static_assert(std::is_trivially_copyable_v<T>);
            __try
            {
                std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof(T));
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        template <class T> auto read(const void* object, std::size_t offset) -> T
        {
            T result{};
            if (!object || !native_read(object, offset, result))
            {
                throw std::runtime_error("kh02_darkside_projectile_read");
            }
            return result;
        }

        auto property(UObject* object, const wchar_t* name, const wchar_t* kind, std::size_t size) -> FProperty*
        {
            auto* field = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!field || field->GetClass().GetName() != kind || field->GetElementSize() != size ||
                field->GetOffset_Internal() < 0)
            {
                throw std::runtime_error("kh02_darkside_projectile_property");
            }
            return field;
        }

        auto object_field(UObject* object, const wchar_t* name) -> UObject*
        {
            return read<UObject*>(object,
                                  property(object, name, L"ObjectProperty", sizeof(UObject*))->GetOffset_Internal());
        }

        auto team(UObject* object) -> int
        {
            auto* field = property(object, L"m_TeamNo", L"IntProperty", sizeof(int));
            if (field->GetOffset_Internal() != actor_team_offset)
            {
                throw std::runtime_error("kh02_darkside_projectile_team_layout");
            }
            return read<int>(object, actor_team_offset);
        }

        auto exact_class(UObject* object, const wchar_t* expected) -> bool
        {
            auto* type = is_uobject_valid_fast(object) ? object->GetClassPrivate() : nullptr;
            return is_uobject_valid_fast(type) && type->GetPathName() == expected;
        }

        auto source_actor(UObject* object) -> bool
        {
            return is_uobject_valid_fast(object) && object->GetPathName().starts_with(source_actor_prefix);
        }

        using SetTeam = void (*)(UObject*, int);

        auto verified_setter(UObject* actor) -> SetTeam
        {
            const auto* image = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
            using kh02_native::Site;
            if (!kh02_native::verify_sites({Site::set_team, Site::actor_team_store, Site::collision_team_store}))
            {
                throw std::runtime_error("kh02_darkside_projectile_profile");
            }
            const auto* table = read<const std::byte*>(actor, 0);
            const auto function = read<std::uintptr_t>(table, set_team_virtual_offset);
            if (function != reinterpret_cast<std::uintptr_t>(image) + kh02_native::rva(Site::set_team))
            {
                throw std::runtime_error("kh02_darkside_projectile_setter_virtual");
            }
            if (property(actor, L"MyAtkColl", L"ObjectProperty", sizeof(UObject*))->GetOffset_Internal() !=
                projectile_collision_offset)
            {
                throw std::runtime_error("kh02_darkside_projectile_collision_layout");
            }
            team(actor);
            return reinterpret_cast<SetTeam>(function);
        }

        struct IgnoredActors
        {
            UObject** data{};
            std::int32_t count{}, capacity{};
        };

        auto ignored_actor_evidence(UObject* collision, UObject* sora, UObject* owner) -> std::pair<bool, bool>
        {
            const auto ignored = read<IgnoredActors>(collision, collision_ignored_actors_offset);
            if (!ignored.data || ignored.count <= 0 || ignored.count > maximum_ignored_actors ||
                ignored.capacity < ignored.count || ignored.capacity > maximum_ignored_actors)
            {
                return {};
            }
            bool includes_sora{};
            bool includes_owner{};
            for (int index = 0; index < ignored.count; ++index)
            {
                auto* actor = read<UObject*>(ignored.data, index * sizeof(UObject*));
                if (!is_uobject_valid_fast(actor))
                {
                    return {};
                }
                includes_sora = includes_sora || actor == sora;
                includes_owner = includes_owner || actor == owner;
            }
            return {includes_sora, includes_owner};
        }
    } // namespace

    struct Kh02DarksideProjectileRepair::Impl
    {
        struct Projectile
        {
            FWeakObjectPtr actor, collision, owner;
            bool repaired{}, failed{};
            int original_team{}, applied_team{};
        };

        const FName projectile_class_name{L"e_dw407_DarkHomingProjectile_C", FNAME_Add};
        FWeakObjectPtr source_world;
        std::vector<Projectile> projectiles;
        std::uint64_t observed{}, repaired{};
        std::string error;

        void reset() noexcept
        {
            auto* world = source_world.Get();
            for (const auto& tracked : projectiles)
            {
                if (!tracked.repaired)
                {
                    continue;
                }
                try
                {
                    auto* actor = tracked.actor.Get();
                    auto* collision = tracked.collision.Get();
                    auto* owner = tracked.owner.Get();
                    if (is_uobject_valid_fast(world) && exact_class(actor, projectile_class_path) &&
                        source_actor(actor) && kh02_actor_world(actor) == world && is_uobject_valid_fast(collision) &&
                        is_uobject_valid_fast(owner) && object_field(actor, L"MyAtkColl") == collision &&
                        object_field(actor, L"Owner") == owner && team(actor) == tracked.applied_team &&
                        read<int>(collision, collision_team_offset) == tracked.applied_team)
                    {
                        verified_setter(actor)(actor, tracked.original_team);
                    }
                }
                catch (const std::exception& failure)
                {
                    error = failure.what();
                }
            }
            projectiles.clear();
            source_world = {};
        }

        void watch(UObject* actor)
        {
            auto* type = is_uobject_valid_fast(actor) ? actor->GetClassPrivate() : nullptr;
            if (!is_uobject_valid_fast(type) || type->GetNamePrivate() != projectile_class_name ||
                !exact_class(actor, projectile_class_path) || !source_actor(actor))
            {
                return;
            }
            auto* world = kh02_actor_world(actor);
            if (!is_uobject_valid_fast(world) || world->GetPathName() != forest_world_path)
            {
                return;
            }
            if (source_world.Get() != world)
            {
                reset();
                source_world = make_uobject_weak(world);
            }
            if (source_world.Get() != world || projectiles.size() >= maximum_tracked_projectiles ||
                std::any_of(projectiles.begin(), projectiles.end(),
                            [actor](const Projectile& tracked) { return tracked.actor.Get() == actor; }))
            {
                return;
            }
            auto reference = make_uobject_weak(actor);
            if (reference.Get() == actor)
            {
                projectiles.push_back({reference});
                ++observed;
            }
        }

        void tick(UObject* sora, UObject* world)
        {
            if (projectiles.empty())
            {
                return;
            }
            if (!is_uobject_valid_fast(world) || source_world.Get() != world ||
                world->GetPathName() != forest_world_path)
            {
                reset();
                return;
            }
            auto* sora_type = is_uobject_valid_fast(sora) ? sora->GetClassPrivate() : nullptr;
            bool possessed{};
            if (is_uobject_valid_fast(sora_type) && sora_type->GetName() == L"p_ex001_Pawn_C" &&
                kh02_actor_world(sora) == world)
            {
                auto* controller = object_field(sora, L"Controller");
                possessed = is_uobject_valid_fast(controller) && object_field(controller, L"Pawn") == sora;
            }
            for (auto& tracked : projectiles)
            {
                auto* actor = tracked.actor.Get();
                if (!is_uobject_valid_fast(actor) || tracked.repaired || tracked.failed)
                {
                    continue;
                }
                try
                {
                    const auto state = read<std::uint32_t>(actor, projectile_state_offset);
                    if (!possessed || !(state & kh02_projectile_reflected_state) ||
                        (state & kh02_projectile_teardown_state))
                    {
                        continue;
                    }
                    auto* owner = object_field(actor, L"Owner");
                    if (!exact_class(actor, projectile_class_path) || !source_actor(actor) ||
                        kh02_actor_world(actor) != world ||
                        (!exact_class(owner, boss_class_path) && !exact_class(owner, first_boss_class_path)) ||
                        !source_actor(owner) || kh02_actor_world(owner) != world)
                    {
                        continue;
                    }
                    const auto apply = verified_setter(actor);
                    auto* collision = object_field(actor, L"MyAtkColl");
                    auto* collision_type = is_uobject_valid_fast(collision) ? collision->GetClassPrivate() : nullptr;
                    if (!is_uobject_valid_fast(collision_type) ||
                        collision_type->GetName() != L"TresAtkCollComponent" || collision->GetOuterPrivate() != actor)
                    {
                        continue;
                    }
                    const auto target = read<FWeakObjectPtr>(actor, projectile_homing_target_offset);
                    const auto [ignores_sora, ignores_owner] = ignored_actor_evidence(collision, sora, owner);
                    const auto desired = kh02_darkside_reflection_team(
                        {state, team(actor), read<int>(collision, collision_team_offset), team(sora), true, possessed,
                         target.Get() == owner, ignores_sora, ignores_owner});
                    if (!desired)
                    {
                        continue;
                    }
                    auto collision_ref = make_uobject_weak(collision);
                    auto owner_ref = make_uobject_weak(owner);
                    if (collision_ref.Get() != collision || owner_ref.Get() != owner)
                    {
                        continue;
                    }
                    tracked.collision = collision_ref;
                    tracked.owner = owner_ref;
                    tracked.original_team = team(actor);
                    tracked.applied_team = *desired;
                    tracked.repaired = true;
                    apply(actor, *desired);
                    if (team(actor) != *desired || read<int>(collision, collision_team_offset) != *desired)
                    {
                        throw std::runtime_error("kh02_darkside_projectile_setter_readback");
                    }
                    ++repaired;
                }
                catch (const std::exception& failure)
                {
                    tracked.failed = true;
                    error = failure.what();
                }
            }
            std::erase_if(projectiles,
                          [](const Projectile& tracked)
                          {
                              auto* actor = tracked.actor.Get();
                              std::uint32_t state{};
                              return !is_uobject_valid_fast(actor) ||
                                     !native_read(actor, projectile_state_offset, state) ||
                                     (state & kh02_projectile_teardown_state);
                          });
        }
    };

    Kh02DarksideProjectileRepair::Kh02DarksideProjectileRepair() : impl(std::make_unique<Impl>()) {}

    Kh02DarksideProjectileRepair::~Kh02DarksideProjectileRepair()
    {
        reset();
    }

    void Kh02DarksideProjectileRepair::watch(UObject* actor)
    {
        impl->watch(actor);
    }

    void Kh02DarksideProjectileRepair::tick(UObject* sora, UObject* world)
    {
        impl->tick(sora, world);
    }

    void Kh02DarksideProjectileRepair::reset() noexcept
    {
        impl->reset();
    }

    auto Kh02DarksideProjectileRepair::needs_frame() const -> bool
    {
        return !impl->projectiles.empty();
    }

    auto Kh02DarksideProjectileRepair::snapshot() const -> nlohmann::json
    {
        return {{"tracked", impl->projectiles.size()},
                {"observed", impl->observed},
                {"repaired", impl->repaired},
                {"error", impl->error}};
    }
} // namespace kh3ap
