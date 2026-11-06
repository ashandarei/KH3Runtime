#pragma once
#include "kh02_challenge_ids.h"
#include "kh3ap/features/kh02/compatibility/kh02_compatibility_protocol.h"
#include <array>
#include <string_view>
#include <utility>

namespace kh3ap::kh02_tracking
{
    namespace native_sites
    {
        enum class Operation
        {
            status,
            complete,
            start,
            unsupported
        };
        enum class Site
        {
            unknown,
            shooting_star,
            castle_highest_camera,
            castle_challenge,
            phantom_enemy,
            forest_challenge,
            forest_gameflow,
            forest_enemy
        };
        enum class Boundary
        {
            begin,
            finish
        };

        inline constexpr std::wstring_view shooting_star_prefix =
            L"/Game/Zlueprints/Gimmick/dw/g_dw_ShootingStar/g_dw_ShootingStar.";
        inline constexpr std::wstring_view castle_challenge_prefix = L"/Game/Zevels/dw/dw_02/umap/dw_02_challenge.";
        inline constexpr std::wstring_view castle_highest_camera_prefix =
            L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms200.";
        inline constexpr std::wstring_view phantom_enemy_prefix = L"/Game/Zevels/dw/dw_07/umap/dw_07_enemy.";
        inline constexpr std::wstring_view forest_challenge_prefix = L"/Game/Zevels/dw/dw_08/umap/dw_08_challenge.";
        inline constexpr std::wstring_view forest_gameflow_prefix = L"/Game/Zevels/dw/dw_08/umap/dw_08_gameflow.";
        inline constexpr std::wstring_view forest_enemy_prefix = L"/Game/Zevels/dw/dw_08/umap/dw_08_enemy.";

        inline auto operation(int adapter) -> Operation
        {
            switch (adapter)
            {
            case static_cast<int>(kh02compat::Adapter::ChallengeStatus):
                return Operation::status;
            case static_cast<int>(kh02compat::Adapter::CompleteChallenge):
                return Operation::complete;
            case static_cast<int>(kh02compat::Adapter::StartChallenge):
                return Operation::start;
            default:
                return Operation::unsupported;
            }
        }

        inline auto site(std::wstring_view caller) -> Site
        {
            for (const auto& [identity, prefix] :
                 std::array{std::pair{Site::shooting_star, shooting_star_prefix},
                            std::pair{Site::castle_highest_camera, castle_highest_camera_prefix},
                            std::pair{Site::castle_challenge, castle_challenge_prefix},
                            std::pair{Site::phantom_enemy, phantom_enemy_prefix},
                            std::pair{Site::forest_challenge, forest_challenge_prefix},
                            std::pair{Site::forest_gameflow, forest_gameflow_prefix},
                            std::pair{Site::forest_enemy, forest_enemy_prefix}})
            {
                if (caller.starts_with(prefix))
                {
                    return identity;
                }
            }
            return Site::unknown;
        }

        // The star actor may read its status; only the original observation
        // camera's completion event can award the corresponding objective.
        inline auto accepts_castle(Operation operation, int id, Site caller) -> bool
        {
            if (caller == Site::shooting_star)
            {
                return operation == Operation::status && id == kh02_challenge_id::look_shooting_star;
            }
            if (caller == Site::castle_highest_camera)
            {
                return operation == Operation::status && id == kh02_challenge_id::reaching_highest_gear_area;
            }
            if (caller != Site::castle_challenge)
            {
                return false;
            }
            if (operation == Operation::status || operation == Operation::complete)
            {
                return id == kh02_challenge_id::reaching_highest_gear_area ||
                       id == kh02_challenge_id::look_shooting_star || id == kh02_challenge_id::get_secret_gear_area;
            }
            if (operation == Operation::start)
            {
                constexpr std::array audited_starts{kh02_challenge_id::kill_shadow,
                                                    kh02_challenge_id::level_up_low,
                                                    kh02_challenge_id::baria_finish,
                                                    kh02_challenge_id::firaga_finish,
                                                    kh02_challenge_id::blizaga_finish,
                                                    kh02_challenge_id::thundaga_finish,
                                                    kh02_challenge_id::blizaga_rail_slide,
                                                    kh02_challenge_id::kill_streak_blizaga,
                                                    kh02_challenge_id::weak_point_attack,
                                                    kh02_challenge_id::flight_battle,
                                                    kh02_challenge_id::excellent_shoot,
                                                    kh02_challenge_id::style_change_finish,
                                                    kh02_challenge_id::first_time_magic_wish,
                                                    kh02_challenge_id::attack_streat_light,
                                                    kh02_challenge_id::reaching_highest_gear_area,
                                                    kh02_challenge_id::look_shooting_star,
                                                    kh02_challenge_id::finish_magic_wish_devil_tower,
                                                    kh02_challenge_id::get_all_treasure_gear_area,
                                                    kh02_challenge_id::get_secret_gear_area};
                for (const auto audited : audited_starts)
                {
                    if (id == audited)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        inline auto accepts_no_damage(int id, Boundary boundary, Site caller) -> bool
        {
            if (id == kh02_challenge_id::no_damage_kill_enemy_aqua)
            {
                return caller == Site::phantom_enemy;
            }
            if (id == kh02_challenge_id::no_damage_rail_slide_thorn_area)
            {
                return caller == (boundary == Boundary::begin ? Site::forest_challenge : Site::forest_gameflow);
            }
            if (id == kh02_challenge_id::no_damage_kill_darkside)
            {
                return caller == (boundary == Boundary::begin ? Site::forest_challenge : Site::forest_enemy);
            }
            return false;
        }
    } // namespace native_sites

    inline bool castle_challenge_site(int operation, int id, std::wstring_view caller)
    {
        return native_sites::accepts_castle(native_sites::operation(operation), id, native_sites::site(caller));
    }

    inline bool no_damage_site(int id, bool start, std::wstring_view caller)
    {
        return native_sites::accepts_no_damage(
            id, start ? native_sites::Boundary::begin : native_sites::Boundary::finish, native_sites::site(caller));
    }

    // Attempt state is armed/finished only by the audited encounter sites above.
    struct NoDamageAttempt
    {
        bool armed{}, hurt{};

        void begin()
        {
            if (!armed)
            {
                armed = true;
                hurt = false;
            }
        }

        void damage(float amount)
        {
            if (armed && amount > 0)
            {
                hurt = true;
            }
        }

        bool finish()
        {
            const bool success = armed && !hurt;
            armed = false;
            return success;
        }

        void reset()
        {
            armed = hurt = false;
        }
    };
} // namespace kh3ap::kh02_tracking
