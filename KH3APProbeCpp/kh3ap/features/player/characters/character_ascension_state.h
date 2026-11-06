#pragma once
#include <string_view>

namespace kh3ap::character_swap
{
    constexpr bool is_ascension_cinematic(std::wstring_view world, std::wstring_view scene)
    {
        return world.find(L"/Game/Levels/bt/bt_04/") != world.npos &&
               scene == L"/Game/Levels/bt/bt_04/umap/bt_04_camera_assension."
                        L"bt_04_camera_assension:PersistentLevel.assention";
    }

    // The owning runtime keeps weak boss/world references. Movie completion is
    // deliberately not a phase-end signal: Ascension continues during gameplay.
    struct CharacterAscensionState
    {
        bool active{};
        int restore_character{-1};
        bool restore_pending{};

        void start(int character)
        {
            if (!active && !restore_pending)
            {
                restore_character = character;
            }
            active = true;
            restore_pending = false;
        }

        void finish()
        {
            if (active)
            {
                active = false;
                restore_pending = restore_character >= 0;
            }
        }

        bool allows_target(int target, int sora) const
        {
            return !active || target == sora;
        }

        void clear()
        {
            *this = {};
        }
    };
} // namespace kh3ap::character_swap
