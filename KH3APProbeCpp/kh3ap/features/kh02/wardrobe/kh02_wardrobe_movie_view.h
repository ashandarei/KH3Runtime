#pragma once
#include "kh3ap/features/kh02/movies/kh02_menu_movie.h"
#include <nlohmann/json.hpp>

namespace kh3ap
{
    class Kh02WardrobeMovieView
    {
      public:
        using Json = nlohmann::json;
        static void labels(RC::Unreal::UObject* root);
        static void info(RC::Unreal::UObject* root, const Json& selection, int region);
        static void histories(RC::Unreal::UObject* root, const std::vector<Json>& single_history,
                              const std::vector<Json>& gradient_history);
        static auto colors(const Json& selection) -> std::vector<kh02menu::MovieArgument>;
        static void append_color(std::vector<kh02menu::MovieArgument>& arguments, const Json& color);
    };
} // namespace kh3ap
