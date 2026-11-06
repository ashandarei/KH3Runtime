#pragma once
#include <cstdint>
#include <span>

namespace kh3ap {
// A view into this module's mapped, uncompressed RCDATA. Missing or incorrectly
// sized resources return an empty view so movie patches can reject them safely.
auto load_embedded_movie_resource(const wchar_t* name, uint32_t expected_size) noexcept
    -> std::span<const uint8_t>;
}
