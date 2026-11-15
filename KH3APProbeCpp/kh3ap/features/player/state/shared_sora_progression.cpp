#include "shared_sora_progression.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include <Unreal/UClass.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap
{
    using namespace RC::Unreal;
    namespace
    {
        template<class T> auto read(uintptr_t address) -> std::optional<T>
        {
            const auto bytes = try_read_process_memory_bytes(address, sizeof(T));
            if (!bytes || bytes->size() != sizeof(T)) return std::nullopt;
            T value{};
            std::memcpy(&value, bytes->data(), sizeof(T));
            return value;
        }
    }

    auto read_shared_sora_progression(UObject* pawn) -> std::optional<SharedSoraProgression>
    {
        const auto build = character_native::build();
        if (build == character_native::Build::Unsupported || !is_uobject_valid_fast(pawn)) return {};
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        auto* player = try_read_reflected_object_property_value(controller, L"Player");
        if (!is_uobject_valid_fast(controller) || !is_uobject_valid_fast(player) ||
            try_read_reflected_object_property_value(controller, L"Pawn") != pawn ||
            try_read_reflected_object_property_value(player, L"PlayerController") != controller) return {};
        const auto address = reinterpret_cast<uintptr_t>(pawn);
        const auto vtable = read<uintptr_t>(address);
        const auto accessor = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
            (build == character_native::Build::Steam ? 0x56d000 : 0x6f8a60);
        constexpr unsigned char signature[]{0x48,0x83,0xec,0x28,0x48,0x8b,0x01,0xff,
                                            0x90,0x40,0x01,0x00,0x00,0x48,0x85,0xc0};
        const auto code = try_read_process_memory_bytes(accessor, sizeof(signature));
        if (!vtable || read<uintptr_t>(*vtable + 0x8c0) != accessor ||
            read<int>(address + 0x514) != 0 || !code ||
            std::memcmp(code->data(), signature, sizeof(signature)) != 0) return {};
        auto* owner = reinterpret_cast<UObject* (*)(UObject*)>(accessor)(pawn);
        if (!is_uobject_valid_fast(owner)) return {};
        const auto owner_ref = make_uobject_weak(owner);
        const auto experience = read<int>(reinterpret_cast<uintptr_t>(owner) + 0x8670);
        if (!experience || *experience < 0 || *experience > 9999999) return {};
        auto* table = find_loaded_runtime_asset(
            L"/Game/DataTable/Player/LevelUp/p_ex001_LevelUpData.p_ex001_LevelUpData");
        if (!is_uobject_valid_fast(table)) return {};
        // Cache only validated scalar thresholds; weak identity invalidates GC/reload.
        static FWeakObjectPtr curve;
        static std::array<int, 99> thresholds{};
        try
        {
            if (curve.Get() != table)
            {
                Kh02DataTableView view(table, L"/Script/TresGame.TresChrLevelUpDataTable", "shared_sora_curve");
                auto* property = view.schema->GetPropertyByNameInChain(L"m_Exp");
                if (!property || property->GetClass().GetName() != L"IntProperty" ||
                    property->GetElementSize() != sizeof(int) || property->GetArrayDim() != 1 ||
                    property->GetOffset_Internal() != 8) return {};
                std::array<int, 99> values{};
                for (size_t index = 0; index < values.size(); ++index)
                {
                    auto* row = view.find(std::to_wstring(index + 1).c_str());
                    if (!row) return {};
                    const int value = *property->ContainerPtrToValuePtr<int>(row);
                    if (value < 0 || value > 9999999 || (index && value < values[index - 1])) return {};
                    values[index] = value;
                }
                thresholds = values;
                curve = make_uobject_weak(table);
            }
        }
        catch (...) { return {}; }
        if (owner_ref.Get() != owner || reinterpret_cast<UObject* (*)(UObject*)>(accessor)(pawn) != owner) return {};
        return SharedSoraProgression{*experience, std::clamp(static_cast<int>(
            std::upper_bound(thresholds.begin(), thresholds.end(), *experience) - thresholds.begin()), 1, 99)};
    }
}
