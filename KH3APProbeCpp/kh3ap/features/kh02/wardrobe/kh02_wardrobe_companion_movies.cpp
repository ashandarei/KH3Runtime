#include "kh02_wardrobe_companion_movies.h"
#include "kh3ap/features/kh02/movies/kh02_menu_movie.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02menu;

    namespace
    {
        constexpr std::size_t game_instance_parameter_limit_bytes = 32;
        constexpr int companion_load_request_base = 0x4b483480;
        constexpr int background_priority = 190;
        constexpr int cursor_priority = 199;
    } // namespace

    auto Kh02WardrobeCompanionMovies::root(std::size_t index) const -> UObject*
    {
        auto* movie = movies[index].Get();
        return is_uobject_valid_fast(movie) ? try_read_reflected_object_property_value(movie, L"Root") : nullptr;
    }

    auto Kh02WardrobeCompanionMovies::prepare(UObject* pawn) -> bool
    {
        constexpr std::array paths{L"/Game/ZI/Scaleform/rebellis02/15_campmenu/CampBackground.CampBackground",
                                   L"/Game/ZI/Scaleform/00_system/Cursor.Cursor"};
        if (request_pawn.Get() != pawn)
        {
            request_pawn = make_uobject_weak(pawn);
            requested = {};
        }
        bool ready = true;
        for (int index = 0; index < companion_count; ++index)
        {
            if (root(index))
            {
                continue;
            }
            auto* asset = find_loaded_runtime_asset(paths[index]);
            if (!asset)
            {
                if (!requested[index])
                {
                    Kh02ReflectedCall instance(L"/Script/Engine.Default__GameplayStatics", L"GetGameInstance",
                                               "Wardrobe", game_instance_parameter_limit_bytes);
                    instance.object(L"WorldContextObject", pawn);
                    instance.run();
                    std::wstring error;
                    if (!request_runtime_asset_load(instance.result(), paths[index], &error,
                                                    companion_load_request_base + index))
                    {
                        throw std::runtime_error("Could not load the original wardrobe background/cursor");
                    }
                    requested[index] = true;
                }
                ready = false;
                continue;
            }
            if (!asset->HasAnyInternalFlags(EInternalObjectFlags::RootSet))
            {
                asset->SetRootSet();
                assets[index] = make_uobject_weak(asset);
            }
            movies[index] = make_uobject_weak(
                open_movie_asset(asset, index == background_index ? background_priority : cursor_priority));
        }
        return ready;
    }

    Kh02WardrobeCompanionMovies::~Kh02WardrobeCompanionMovies()
    {
        for (auto& asset : assets)
        {
            if (auto* object = asset.Get(); is_uobject_valid_fast(object))
            {
                object->ClearRootSet();
            }
        }
    }

    void Kh02WardrobeCompanionMovies::show()
    {
        for (std::size_t index = 0; index < companion_count; ++index)
        {
            try_gfx_set_member_value_bool(gfx, root(index), L"visible", true);
        }
        invoke_movie(find_movie_member(root(background_index), L"campMenuBG"), L"gotoAndPlay", {std::wstring(L"in")});
    }

    void Kh02WardrobeCompanionMovies::hide()
    {
        for (std::size_t index = 0; index < companion_count; ++index)
        {
            if (auto* movie_root = root(index))
            {
                try_gfx_set_member_value_bool(gfx, movie_root, L"visible", false);
            }
        }
    }
} // namespace kh3ap
