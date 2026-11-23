#include "kh3ap/features/player/controller_abilities/models.h"

#include "kh3ap/core/objects/actor_owning_world.h"
#include <Unreal/UObject.hpp>
#include <algorithm>

namespace kh3ap::controller_ability_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    auto unlock_key(std::wstring_view runtime_token) -> std::wstring_view
    {
        return runtime_token.starts_with(s_unlock_token_prefix)
                   ? runtime_token.substr(s_unlock_token_prefix.size())
                   : std::wstring_view{};
    }

    auto current_wall_run_id(UObject* pawn) -> uint8_t
    {
        auto* world = actor_owning_world(pawn);
        const std::wstring path = world ? world->GetPathName() : L"";
        constexpr std::wstring_view prefix = L"/Game/Levels/";
        std::wstring_view package = path;
        if (package.starts_with(L"/Game/Mods/Coliseum/"))
        {
            return find_definition(L"wall_run_he")->ability_value;
        }
        if (!package.starts_with(prefix))
        {
            return ability_id::wall_run;
        }
        package.remove_prefix(prefix.size());
        auto group = package.substr(0, package.find(L'/'));
        if (group == L"kg_DLC" || group == L"bt_DLC")
        {
            group = L"remind";
        }
        else if (group == L"bt" || group == L"ew")
        {
            group = L"kg";
        }
        else if (group.ends_with(L"_DLC"))
        {
            group.remove_suffix(4);
        }
        for (const auto& definition : s_world_wall_runs)
        {
            if (definition.key.substr(std::wstring_view{L"wall_run_"}.size()) == group)
            {
                return definition.ability_value;
            }
        }
        return ability_id::wall_run;
    }

    auto find_definition(std::wstring_view key) -> const ControllerAbilityDefinition*
    {
        if (key == STR("links"))
        {
            return &s_link_abilities.front();
        }
        for (const auto& definition : s_world_wall_runs)
        {
            if (definition.key == key)
            {
                return &definition;
            }
        }
        const auto match =
            std::find_if(s_controller_abilities.begin(), s_controller_abilities.end(),
                         [key](const ControllerAbilityDefinition& definition)
                         {
                             return definition.key == key;
                         });
        return match == s_controller_abilities.end() ? nullptr : &*match;
    }
} // namespace kh3ap::controller_ability_detail
