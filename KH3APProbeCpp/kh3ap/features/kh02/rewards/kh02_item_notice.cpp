#include "kh02_item_notice.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FMemory.hpp>
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstring>
#include <optional>

namespace kh3ap::kh02rewards
{
    using namespace RC::Unreal;

    namespace
    {
        template <typename T> auto try_read_process_value(uint64_t address) -> std::optional<T>
        {
            T value{};
            SIZE_T copied{};
            if (!looks_like_process_pointer(address) ||
                !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &value, sizeof(value),
                                   &copied) ||
                copied != sizeof(value))
            {
                return std::nullopt;
            }
            return value;
        }
    } // namespace

    auto present_inventory_item(UObject* aqua, UObject* owner, int32_t item_id, bool inventory_full) -> bool
    {
        const auto module_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        using kh02_native::Site;
        if (!kh02_native::verify_sites({Site::item_show, Site::item_open}))
        {
            return false;
        }
        const auto show_address = module_base + kh02_native::rva(Site::item_show);
        const auto open_address = module_base + kh02_native::rva(Site::item_open);
        auto* controller = try_read_reflected_object_property_value(aqua, L"Controller");
        if (!is_uobject_valid_fast(controller) || try_read_reflected_object_property_value(controller, L"Pawn") != aqua)
        {
            return false;
        }
        const auto left = is_uobject_valid_fast(owner)
                              ? try_read_process_value<uint64_t>(reinterpret_cast<uint64_t>(owner) + 0x80)
                              : std::nullopt;
        auto* left_object = left ? reinterpret_cast<UObject*>(*left) : nullptr;
        const auto panel =
            is_uobject_valid_fast(left_object) ? try_read_process_value<uint64_t>(*left + 0x1e8) : std::nullopt;
        auto* panel_object = panel ? reinterpret_cast<UObject*>(*panel) : nullptr;
        if (!is_uobject_valid_fast(panel_object) ||
            panel_object->GetClassPrivate()->GetName() != L"TresUIP_HudLeftGetItem")
        {
            return false;
        }
        const auto active = try_read_process_value<uint8_t>(*panel + 0xe8);
        if (!active)
        {
            return false;
        }
        if (*active)
        {
            // This is the native BP_TresGetItem queue node and allocator.
            // Its normal update owns display order and frees each node.
            struct Notice
            {
                Notice* next{};
                int32_t item{};
                bool full{};
                std::array<uint8_t, 3> padding{};
            };

            static_assert(sizeof(Notice) == 16);
            const auto tail = try_read_process_value<uint64_t>(*panel + 0x110);
            if (!tail || !looks_like_process_pointer(*tail))
            {
                return false;
            }
            auto* notice = static_cast<Notice*>(FMemory::Malloc(sizeof(Notice)));
            if (!notice)
            {
                return false;
            }
            *notice = Notice{nullptr, item_id, inventory_full};
            std::memcpy(reinterpret_cast<void*>(*panel + 0x110), &notice, sizeof(notice));
            std::atomic_thread_fence(std::memory_order_release);
            std::memcpy(reinterpret_cast<void*>(*tail), &notice, sizeof(notice));
        }
        else
        {
            reinterpret_cast<void (*)(UObject*, int32_t, bool)>(show_address)(panel_object, item_id, inventory_full);
            reinterpret_cast<void (*)(UObject*)>(open_address)(panel_object);
            *reinterpret_cast<uint8_t*>(*left + 0x268) = 1;
        }
        return true;
    }
} // namespace kh3ap::kh02rewards
