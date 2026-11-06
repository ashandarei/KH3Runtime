#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeEquipmentAbilitySnapshot
    {
        std::unordered_map<std::wstring, uint64_t> counts{};
        std::vector<std::wstring> sources{};
        std::wstring equipment_name{};
        std::wstring status{L"missing_equipment"};
    };

    auto try_read_process_memory_bytes(uint64_t address, size_t size) -> std::optional<std::vector<std::byte>>;

}
