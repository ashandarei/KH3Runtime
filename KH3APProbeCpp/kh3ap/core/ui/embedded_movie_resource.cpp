#include <span>
#include "kh3ap/core/ui/embedded_movie_resource.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace kh3ap
{
    auto load_embedded_movie_resource(const wchar_t* name, uint32_t expected_size) noexcept
        -> std::span<const uint8_t>
    {
        const auto module = reinterpret_cast<HMODULE>(&__ImageBase);
        const auto resource = FindResourceW(module, name, reinterpret_cast<LPCWSTR>(RT_RCDATA));
        if (!resource || !expected_size || SizeofResource(module, resource) != expected_size)
        {
            return {};
        }
        const auto loaded = LoadResource(module, resource);
        const auto* data = loaded ? static_cast<const uint8_t*>(LockResource(loaded)) : nullptr;
        return data ? std::span<const uint8_t>{data, expected_size} : std::span<const uint8_t>{};
    }
} // namespace kh3ap
