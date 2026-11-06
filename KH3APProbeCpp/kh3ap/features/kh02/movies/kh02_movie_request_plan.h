#pragma once
#include "kh02/story_movie_metadata.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap
{
    struct Kh02MovieRequestPlan
    {
        std::wstring logical;
        const kh02movie_metadata::Movie* story{};
        std::string file;
    };

    inline auto kh02_movie_request_plan(std::wstring logical, std::string_view language = "en") -> Kh02MovieRequestPlan
    {
        std::replace(logical.begin(), logical.end(), L'\\', L'/');
        for (const auto& row : kh02movie_metadata::stories)
        {
            if (logical == row.logical)
            {
                std::string file = row.file;
                if (file.starts_with("en/"))
                {
                    if (language.empty() || language.size() > 16 ||
                        !std::all_of(language.begin(), language.end(),
                                     [](char value) { return value >= 'a' && value <= 'z'; }))
                    {
                        throw std::runtime_error("movie_bridge_language_invalid");
                    }
                    file = std::string(language) + file.substr(2);
                }
                return {std::move(logical), &row, std::move(file)};
            }
        }
        for (const auto& [identity, file] :
             {std::pair{L"Rebellis02/gear_a.usm", "gear_a.usm"}, std::pair{L"Rebellis02/gear_b.usm", "gear_b.usm"},
              std::pair{L"Rebellis02/gear_c.usm", "gear_c.usm"}, std::pair{L"Rebellis02/gear_d.usm", "gear_d.usm"}})
        {
            if (logical == identity)
            {
                return {std::move(logical), nullptr, file};
            }
        }
        throw std::runtime_error("movie_bridge_movie_not_audited");
    }
} // namespace kh3ap
