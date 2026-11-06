#include "kh02_challenge_movie_lease.h"
#include "kh3ap/features/kh02/movies/kh02_menu_movie.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02menu;

    namespace
    {
        constexpr int challenge_movie_priority = 195;
        constexpr int challenge_movie_load_request_id = 0x4b483470;
        constexpr auto asset_path = L"/Game/ZI/Scaleform/rebellis02/15_campmenu/CampMenu.CampMenu";
    } // namespace

    auto Kh02ChallengeMovieLease::root() const -> UObject*
    {
        auto* player = movie.Get();
        return is_uobject_valid_fast(player) ? try_read_reflected_object_property_value(player, L"Root") : nullptr;
    }

    Kh02ChallengeMovieLease::~Kh02ChallengeMovieLease()
    {
        hide();
        if (auto* movie_asset = asset.Get(); is_uobject_valid_fast(movie_asset))
        {
            movie_asset->ClearRootSet();
        }
    }

    auto Kh02ChallengeMovieLease::prepare(UObject* pawn) -> bool
    {
        if (is_uobject_valid_fast(movie.Get()))
        {
            return prepare_menu();
        }

        auto* movie_asset = find_loaded_runtime_asset(asset_path);
        if (!movie_asset)
        {
            auto* world = static_cast<AActor*>(pawn)->GetWorld();
            if (requested && (request_pawn.Get() != pawn || request_world.Get() != world))
            {
                // A latent load on a departing pawn/world can be cancelled.
                // A changed owner permits one new request, without a timer.
                requested = false;
            }
            if (!requested)
            {
                std::wstring load_error;
                if (!request_runtime_asset_load(pawn, asset_path, &load_error, challenge_movie_load_request_id))
                {
                    throw std::runtime_error("kh02_challenge_load:" + to_utf8(load_error));
                }
                requested = true;
                request_pawn = make_uobject_weak(pawn);
                request_world = make_uobject_weak(world);
            }
            return false;
        }
        if (!movie_asset->HasAnyInternalFlags(EInternalObjectFlags::RootSet))
        {
            movie_asset->SetRootSet();
            asset = make_uobject_weak(movie_asset);
        }
        movie = make_uobject_weak(open_movie_asset(movie_asset, challenge_movie_priority));
        return prepare_menu();
    }

    auto Kh02ChallengeMovieLease::ready() const -> bool
    {
        auto* player = movie.Get();
        return is_uobject_valid_fast(player) && prepared_movie.Get() == player;
    }

    auto Kh02ChallengeMovieLease::prepare_menu() -> bool
    {
        if (ready())
        {
            return true;
        }
        auto* root_object = root();
        if (!is_uobject_valid_fast(root_object) ||
            !find_movie_member(root_object, L"campMenu.contents.menuChallenge.challengeListAnm.challengeList"))
        {
            return false;
        }
        // Prepare the shell hidden, once per movie. Page openings own only their
        // contents transition, without replaying the whole camp animation.
        invoke_movie(root_object, L"openCampMenu",
                     {false, std::wstring{}, std::wstring{}, std::wstring{}, std::wstring(L"KINGDOM HEARTS 0.2")});
        // openCampMenu makes its root visible as part of initialization.
        // Prewarming must leave visibility with the actual overlay request.
        try_gfx_set_member_value_bool(gfx, root_object, L"visible", false);
        prepared_movie = movie;
        return true;
    }

    void Kh02ChallengeMovieLease::hide_native(UObject* camp)
    {
        native_visibility.suspend(camp);
    }

    void Kh02ChallengeMovieLease::hide()
    {
        if (auto* root_object = root())
        {
            try_gfx_set_member_value_bool(gfx, root_object, L"visible", false);
        }
        native_visibility.restore();
    }
} // namespace kh3ap
