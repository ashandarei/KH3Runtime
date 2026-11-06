#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"

#include "kh3ap/app/enums.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/types.h"

#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UScriptStruct.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <sstream>

#define NOMINMAX
#include <windows.h>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_ability_snapshot_bytes = 4096;
    }

    auto try_read_process_memory_bytes(uint64_t address, size_t size) -> std::optional<std::vector<std::byte>>
    {
        if (address == 0 || size == 0 || size > max_ability_snapshot_bytes)
        {
            return std::nullopt;
        }

        std::vector<std::byte> buffer(size);
        SIZE_T bytes_read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), buffer.data(), buffer.size(), &bytes_read) ||
            bytes_read != buffer.size())
        {
            return std::nullopt;
        }
        return buffer;
    }

}
