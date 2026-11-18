#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace kh3ap::character_cinematic_binding
{
    struct PlayerRole
    {
        bool presentation_only{};
        bool actor_is_player{};
        uintptr_t actor{};
    };

    constexpr bool presentation_track(std::wstring_view type)
    {
        return type == L"TresInterpTrackVisibility" || type == L"TresInterpTrackWeaponControl";
    }

    constexpr bool proportion_animation_track(std::wstring_view type)
    {
        // Component and weapon tracks have their own receiving meshes. They
        // must not inherit the player's skeletal retargeting policy.
        return type == L"TresInterpTrackFullBodyAnimation" ||
               type == L"TresInterpTrackSlotAnimation" ||
               type == L"TresInterpTrackFaceAnimation" ||
               type == L"TresInterpTrackEyeAnimation" ||
               type == L"TresInterpTrackLipAnimation";
    }

    constexpr bool can_share_player_roles(std::wstring_view scene,
                                          std::span<const PlayerRole> roles)
    {
        if (!scene.starts_with(L"/Game/Levels/") || roles.size() < 2 || roles.size() > 512)
        {
            return false;
        }
        uintptr_t original_player = 0;
        for (const auto& role : roles)
        {
            // Separate authored actors must not collapse into one player.
            // Empty slots are resolved to the same tagged pawn by Tres.
            if (!role.presentation_only || (role.actor && !role.actor_is_player) ||
                (role.actor && original_player && role.actor != original_player))
            {
                return false;
            }
            if (role.actor)
            {
                original_player = role.actor;
            }
        }
        return true;
    }
} // namespace kh3ap::character_cinematic_binding
