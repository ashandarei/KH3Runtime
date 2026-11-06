#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/story/world_fixes/big_hero_rescue_damage.h"
#include "kh3ap/features/story/world_fixes/big_hero_rescue_damage_policy.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/combat/kairi_ground_slam.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/NameTypes.hpp>
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap
{
    using namespace RC::Unreal;

    struct BigHeroRescueDamage::Impl
    {
        static constexpr size_t maximum_owner_class_depth = 32;
        using Damage = void (*)(UObject*, float, UObject*, const void*, const void*, const void*, UObject*);
        inline static Impl* active{};
        uint64_t original{};
        std::unique_ptr<PLH::x64Detour> hook;
        FName hit_counter;
        std::array<FName, big_hero_rescue_damage_classes.size()> class_names;

        bool blocked(UObject* component) const
        {
            if (!is_uobject_valid_fast(component) || component->GetNamePrivate() != hit_counter)
            {
                return false;
            }
            auto* owner = component->GetOuterPrivate();
            if (!is_uobject_valid_fast(owner))
            {
                return false;
            }
            auto* type = owner->GetClassPrivate();
            for (size_t depth = 0; depth < maximum_owner_class_depth && is_uobject_valid_fast(type);
                 ++depth)
            {
                for (size_t index = 0; index < class_names.size(); ++index)
                {
                    if (type->GetNamePrivate() == class_names[index] &&
                        block_big_hero_rescue_damage(L"TresGimmickHitCount", type->GetPathName()))
                    {
                        return true;
                    }
                }
                type = static_cast<UClass*>(type->GetSuperStruct());
            }
            return false;
        }

        static void damage(UObject* component, float amount, UObject* controller,
                           const void* hit, const void* info, const void* direction, UObject* causer)
        {
            auto& self = *active;
            if (self.blocked(component))
            {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] BigHeroRescue blocked ordinary damage owner={} component={}\n"),
                    component->GetOuterPrivate()->GetPathName(), component->GetName());
#endif
                return;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            kairi_ground_slam::observe_gimmick_damage(component, controller, info, causer);
#endif
            reinterpret_cast<Damage>(self.original)(component, amount, controller, hit, info, direction, causer);
        }

        void install()
        {
            if (hook)
            {
                return;
            }
            if (!character_native::verify_sites({character_native::Site::gimmick_damage}))
            {
                RC::Output::send<RC::LogLevel::Error>(STR("[KH3APProbeCpp] BigHeroRescue damage filter rejected: native signature\n"));
                return;
            }
            hit_counter = FName(L"TresGimmickHitCount", FNAME_Add);
            for (size_t index = 0; index < class_names.size(); ++index)
            {
                const auto path = big_hero_rescue_damage_classes[index];
                class_names[index] = FName(path.substr(path.find_last_of(L'.') + 1).data(), FNAME_Add);
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            active = this;
            hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(character_native::Site::gimmick_damage),
                                                  reinterpret_cast<uintptr_t>(&damage), &original);
            const bool installed = hook->hook() && original;
            if (!installed)
            {
                uninstall();
            }
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] BigHeroRescue damage filter installed={}\n"), installed);
        }

        void uninstall()
        {
            if (hook && hook->isHooked())
            {
                hook->unHook();
            }
            hook.reset();
            if (active == this)
            {
                active = nullptr;
            }
        }
    };

    BigHeroRescueDamage::BigHeroRescueDamage() : impl_(std::make_unique<Impl>()) {}
    BigHeroRescueDamage::~BigHeroRescueDamage() { impl_->uninstall(); }
    void BigHeroRescueDamage::install() { impl_->install(); }
    void BigHeroRescueDamage::uninstall() { impl_->uninstall(); }
}
