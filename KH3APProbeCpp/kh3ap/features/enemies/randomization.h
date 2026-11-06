#pragma once

#include <nlohmann/json.hpp>

namespace kh3ap::randomization
{
    // Pure seed generation. No filesystem, engine state, load order or global RNG.
    auto validate_settings(const nlohmann::json& settings) -> void;
    auto generate(const nlohmann::json& settings) -> nlohmann::json;
}
