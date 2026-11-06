#include "kh3ap/features/player/controller_abilities/models.h"

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

    auto find_definition(std::wstring_view key) -> const ControllerAbilityDefinition*
    {
        if (key == STR("links"))
        {
            return &s_link_abilities.front();
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
