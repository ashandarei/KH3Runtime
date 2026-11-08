#include "kh02_mirror_travel.h"
#include "kh02_story_flags.h"
#include "kh3ap/features/kh02/world/kh02_area_resume.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    using RC::Unreal::UObject;

    auto Kh02MirrorTravel::stage(UObject* world, std::wstring_view caller, const std::wstring& destination,
                                 const std::wstring& tag, int fade, bool epilogue_complete) -> bool
    {
        const auto world_path = is_uobject_valid_fast(world) ? world->GetPathName() : std::wstring{};
        kh02_mirror_travel::Plan plan;
        try
        {
            plan = kh02_mirror_travel::resolve(caller, world_path, destination, tag, fade, epilogue_complete, pending);
        }
        catch (const kh02_mirror_travel::UnhostedDestination&)
        {
            throw std::runtime_error("kh02_mirror_destination_not_hosted:" + to_utf8(destination));
        }
        const bool special_battle_clear = plan.special_battle_clear;
        staged = std::move(plan);
        source_world = make_uobject_weak(world);
        return special_battle_clear;
    }

    void Kh02MirrorTravel::commit()
    {
        pending = staged.has_value();
    }

    auto Kh02MirrorTravel::starts_boss_rush() const -> bool
    {
        return staged && staged->boss_rush_entry;
    }

    void Kh02MirrorTravel::cancel()
    {
        pending = false;
        staged.reset();
        source_world = {};
    }

    auto Kh02MirrorTravel::dispatch(UObject* current_world, const Kh02StoryFlags& source_flags) -> bool
    {
        if (!pending || !staged || source_world.Get() != current_world)
        {
            return false;
        }
        pending = false;
        // Copy before ProcessEvent: source callbacks may cancel or replace this request.
        auto plan = *staged;
        if (plan.package == L"/Game/Maps/APKH02Mirrors/APKH02Forest" && plan.tag == L"dw_08_Lv_Start_01")
        {
            plan.tag = kh02_area_resume_tag(8, source_flags.value(L"gameflow_DW"), plan.tag.c_str());
        }
        arrival_tag = plan.tag;
        arrival_package = plan.package;
        constexpr std::size_t travel_parameter_limit_bytes = 2'048;
        Kh02ReflectedCall travel(L"/Script/TresGame.Default__TresGameBlueprintLibrary", L"TresMapJump", "kh02_story",
                                 travel_parameter_limit_bytes);
        travel.name(L"MapName", plan.package.c_str());
        travel.name(L"TargetTag", plan.tag.c_str());
        travel.integer(L"InFadeKind", plan.fade);
        travel.run();
        return true;
    }
} // namespace kh3ap
