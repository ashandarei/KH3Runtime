#pragma once

#include "kh02/mirror_host_metadata.h"
#include <string_view>

namespace kh3ap::kh02_world_identity
{
    // Compare package.object identities without constructing a string for every
    // candidate. Exact boundaries matter: streamed actors are not host worlds.
    inline auto mirror_room(std::wstring_view world) noexcept -> const kh02mirror_metadata::Room*
    {
        for (const auto& room : kh02mirror_metadata::rooms)
        {
            const std::wstring_view package = room.package;
            const std::wstring_view name = room.name;
            if (world.size() == package.size() + 1 + name.size() && world.starts_with(package) &&
                world[package.size()] == L'.' && world.substr(package.size() + 1) == name)
            {
                return &room;
            }
        }
        return nullptr;
    }
} // namespace kh3ap::kh02_world_identity
