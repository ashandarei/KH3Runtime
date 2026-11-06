#pragma once
#include "kh02/ending_runtime_metadata.h"
#include <string_view>
#include <utility>

namespace kh3ap
{
    inline auto kh02_scene_package_actor(std::wstring_view path, std::wstring_view package) -> bool
    {
        return path.starts_with(package) && path.size() > package.size() && path[package.size()] == L'.';
    }

    struct Kh02SceneBinding
    {
        bool camera{}, scene{}, opening{};
        const wchar_t* group{};

        auto accepted() const -> bool
        {
            return camera || scene;
        }
    };

    inline auto kh02_scene_binding(std::wstring_view actor_path, std::wstring_view actor_name) -> Kh02SceneBinding
    {
        const bool camera = actor_path.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms040.") &&
                            (actor_name == L"ms040s" || actor_name == L"ms040e");
        const kh02ending_metadata::Scene* ending = nullptr;
        for (const auto& candidate : kh02ending_metadata::scenes)
        {
            if (kh02_scene_package_actor(actor_path, candidate.package) && actor_name == candidate.actor)
            {
                ending = &candidate;
            }
        }
        const bool scene = ending ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw011.") ||
                           (actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw013.") &&
                            actor_name == L"dw013_1") ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw03_dw016.") ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw07_dw019.") ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw03_dw020.") ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw07_dw017.") ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw07_dw018.") ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw021.") ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw022.") ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw023.") ||
                           actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw024.");
        const bool opening = actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw011.");
        const auto* group = opening  ? L"TresInterpGroup_22"
                            : ending ? ending->group
                            : actor_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw022.")
                                ? L"TresInterpGroup_2"
                                : L"TresInterpGroup_0";

        return {camera, scene, opening, group};
    }

    inline auto kh02_scene_full_movie(std::wstring_view path) -> const char*
    {
        for (const auto& [prefix, movie] :
             {std::pair{L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw025a.", "Rebellis02/ja/cs_dw025a_mv.usm"},
              std::pair{L"/Game/Zutscene/Localization/en/dw/cutscene_dw08_dw025b.", "Rebellis02/ja/cs_dw025b_mv.usm"},
              std::pair{L"/Game/Zutscene/Localization/en/dw/cutscene_dw09_dw026.", "Rebellis02/ja/cs_dw026_mv.usm"},
              std::pair{L"/Game/Zutscene/Localization/en/dw/cutscene_dw51_dw027.", "Rebellis02/ja/cs_dw027_mv.usm"},
              std::pair{L"/Game/Zutscene/Localization/en/dw/cutscene_dw53_dw033.", "Rebellis02/ja/cs_dw033_mv.usm"},
              std::pair{L"/Game/Zutscene/Localization/en/dw/cutscene_dw54_dw034.", "Rebellis02/ja/cs_dw034_mv.usm"}})
        {
            if (path.starts_with(prefix))
            {
                return movie;
            }
        }
        return nullptr;
    }
} // namespace kh3ap
