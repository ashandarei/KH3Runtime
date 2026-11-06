#include <memory>
#include <utility>
#include <cstddef>
#include <algorithm>
#include "kh3ap/features/world/space/space_battle_markers.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/space/space_marker_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/gfx/gfx_value_layout.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <array>
#include <cstring>
#include <limits>

namespace kh3ap
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto steam_set_color_rva = 0x5080250;
        constexpr auto egs_set_color_rva = 0x5971ae0;
        constexpr auto steam_display_object_guard_rva = 0x5085fa0;
        constexpr auto egs_display_object_guard_rva = 0x5977830;
        constexpr auto gfx_value_traits_offset = 0x28;
        constexpr auto gfx_traits_kind_offset = 0x90;
        constexpr auto gfx_display_traits_first_kind = 0x18u;
        constexpr auto gfx_display_traits_kind_count = 12u;
        constexpr auto gfx_traits_flags_offset = 0x88;
        constexpr auto gfx_traits_removed_mask = 0x20;
        constexpr auto gfx_value_display_object_offset = 0x88;
        constexpr auto gfx_display_guard_vtable_offset = 0x1a8;
        constexpr auto enemy_guide_slots_offset = 808;
        constexpr auto enemy_guide_slot_bytes = 88;
        constexpr auto enemy_guide_icon_offset = 24;
        constexpr auto rgb_channel_count = 3;
        constexpr std::array completed_marker_additive_color{48.f / 255.f, 168.f / 255.f,
                                                             88.f / 255.f};
        constexpr auto set_color_call_displacement_offset = 23;
        constexpr std::array<uint8_t, 3> egs_set_color_call_displacement{0xe5, 0x17, 0xf6};

        constexpr auto space_battle_marker_slot_count = 10;
        struct alignas(16) ColorTransform
        {
            std::array<float, 4> multiply, add;
        };
        static_assert(sizeof(ColorTransform) == 32);
        using SetColor = void (*)(void*, const ColorTransform*);
        uint64_t original_color{};
        bool color_writer_supported()
        {
            const auto& p = space::native_profile();
            if (!p.supported)
            {
                return false;
            }
            auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            // DisplayObject::SetCxform, including both calls and the aligned stores.

            std::array<uint8_t, 48> bytes{
                0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda, 0xe8, 0x72, 0x39,
                0,    0,    0xba, 2,    0,    0,    0,    0x48, 0x8b, 0xc8, 0xe8, 0xb5,
                0xb8, 0xf5, 0xff, 0x0f, 0x28, 3,    0x0f, 0x29, 0x40, 0x50, 0x0f, 0x28,
                0x4b, 0x10, 0x0f, 0x29, 0x48, 0x60, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};
            if (!p.steam)
            {
                std::copy(egs_set_color_call_displacement.begin(),
                          egs_set_color_call_displacement.end(),
                          bytes.begin() + set_color_call_displacement_offset);
            }
            return !std::memcmp(
                reinterpret_cast<void*>(base + (p.steam ? steam_set_color_rva : egs_set_color_rva)),
                bytes.data(), bytes.size());
        }
        void* display_object(UObject* icon)
        {
            // Match GFx's display-value/type/removal guards. These pointers are used
            // only synchronously on the game thread, never retained across frames.
            const auto& p = space::native_profile();
            auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            __try
            {
                auto* object = reinterpret_cast<uint8_t*>(icon);
                if ((*reinterpret_cast<uint32_t*>(object + gfx_native_value::type_offset) &
                     gfx_native_value::value_type_mask) !=
                    gfx_native_value::display_object_value_type)
                {
                    return nullptr;
                }
                auto* value = *reinterpret_cast<uint8_t**>(object + gfx_native_value::value_offset);
                if (!value)
                {
                    return nullptr;
                }
                auto* traits = *reinterpret_cast<uint8_t**>(value + gfx_value_traits_offset);
                if (!traits ||
                    *reinterpret_cast<uint32_t*>(traits + gfx_traits_kind_offset) -
                            gfx_display_traits_first_kind >=
                        gfx_display_traits_kind_count ||
                    (traits[gfx_traits_flags_offset] & gfx_traits_removed_mask))
                {
                    return nullptr;
                }
                auto* display =
                    *reinterpret_cast<uint8_t**>(value + gfx_value_display_object_offset);
                if (!display)
                {
                    return nullptr;
                }
                auto* vtable = *reinterpret_cast<uintptr_t**>(display);
                if (!vtable || vtable[gfx_display_guard_vtable_offset / sizeof(void*)] !=
                                   base + (p.steam ? steam_display_object_guard_rva
                                                   : egs_display_object_guard_rva))
                {
                    return nullptr;
                }
                return display;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }
        bool write_color_only(UObject* icon, const ColorTransform& color)
        {
            auto* display = display_object(icon);
            if (!display || !original_color)
            {
                return false;
            }
            // Bypass our timeline filter for explicit tint/restoration writes.
            reinterpret_cast<SetColor>(original_color)(display, &color);
            return true;
        }
        bool transform(UObject* icon, ColorTransform& value, bool write)
        {
            if (!is_uobject_valid_fast(icon))
            {
                return false;
            }
            if (write)
            {
                return write_color_only(icon, value);
            }
            auto* fn = icon->GetFunctionByNameInChain(L"GetColorTransform");
            auto* p = is_uobject_valid_fast(fn) ? find_function_param_by_name(fn, L"ReturnValue")
                                                : nullptr;
            if (!p || !p->IsA<FStructProperty>() || p->GetOffset_Internal() != 0 ||
                p->GetElementSize() != sizeof(ColorTransform) ||
                fn->GetParmsSize() != sizeof(ColorTransform))
            {
                return false;
            }
            auto* structure = static_cast<FStructProperty*>(p)->GetStruct();
            auto* mul = structure ? find_struct_field_by_name(structure, L"Multiply") : nullptr;
            auto* add = structure ? find_struct_field_by_name(structure, L"Add") : nullptr;
            if (!mul || !add || mul->GetOffset_Internal() != 0 ||
                add->GetOffset_Internal() != offsetof(ColorTransform, add) ||
                mul->GetElementSize() != sizeof(value.multiply) ||
                add->GetElementSize() != sizeof(value.add))
            {
                return false;
            }
            // KH3 copies these floats into the normalized render Cxform unchanged.
            // ActionScript's byte-sized offsets would overdrive the HUD's glow here.
            ColorTransform params{};
            icon->ProcessEvent(fn, &params);
            value = params;
            return true;
        }
        bool restore_color(UObject* icon, const ColorTransform& original)
        {
            ColorTransform current{};
            if (!transform(icon, current, false))
            {
                return false;
            }
            for (size_t i = 0; i < rgb_channel_count; ++i)
            {
                current.multiply[i] = original.multiply[i];
                current.add[i] = original.add[i];
            }
            return transform(icon, current, true);
        }
        FWeakObjectPtr weak(UObject* object)
        {
            FWeakObjectPtr result{};
            if (is_uobject_valid_fast(object))
            {
                using Assign = void (*)(FWeakObjectPtr*, UObject*);
                auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                reinterpret_cast<Assign>(base + space::native_profile().weak_assign_rva)(&result,
                                                                                         object);
            }
            return result;
        }
    } // namespace

    struct RuntimeSpaceBattleMarkers::Impl
    {
        using Front = void (*)(UObject*, void*, UObject*, int32_t, int32_t, bool);
        using Behind = void (*)(UObject*, void*, UObject*, int32_t, float, int32_t, bool);
        inline static Impl* self{};
        Completed completed;
        Revision revision;
        std::unique_ptr<PLH::x64Detour> front, behind, color;
        DWORD game_thread{};
        uint64_t original_front{}, original_behind{}, updates{};
        FWeakObjectPtr guide;
        struct Slot
        {
            FWeakObjectPtr target, icon;
            uintptr_t display_identity{};
            std::wstring battle;
            ColorTransform original{};
            bool known{}, green{};
            int proximity{-1};
            uint64_t revision{std::numeric_limits<uint64_t>::max()};
        };
        std::array<Slot, space_battle_marker_slot_count> slots{};
        explicit Impl(Completed c, Revision r) : completed(std::move(c)), revision(std::move(r))
        {
        }
        ~Impl()
        {
            if (front)
            {
                front->unHook();
            }
            if (behind)
            {
                behind->unHook();
            }
            for (auto& slot : slots)
            {
                if (slot.green)
                {
                    restore_color(slot.icon.Get(), slot.original);
                }
            }
            if (color)
            {
                color->unHook();
            }
            original_color = 0;
            if (self == this)
            {
                self = nullptr;
            }
        }
        static void front_hook(UObject* owner, void* slot, UObject* target, int32_t index,
                               int32_t level, bool boss)
        {
            auto& s = *self;
            reinterpret_cast<Front>(s.original_front)(owner, slot, target, index, level, boss);
            s.update(owner, slot, target);
        }
        static void behind_hook(UObject* owner, void* slot, UObject* target, int32_t index,
                                float distance, int32_t level, bool boss)
        {
            auto& s = *self;
            reinterpret_cast<Behind>(s.original_behind)(owner, slot, target, index, distance, level,
                                                        boss);
            s.update(owner, slot, target);
        }
        static void color_hook(void* display, const ColorTransform* incoming)
        {
            auto& s = *self;
            // Timeline updates can follow the guide helpers. Override only RGB on
            // a currently tracked cleared icon; all alpha and animation updates
            // still come from the native timeline. Other UI takes the original path.
            if (GetCurrentThreadId() == s.game_thread)
            {
                for (auto& slot : s.slots)
                {
                    if (!slot.green ||
                        slot.display_identity != reinterpret_cast<uintptr_t>(display))
                    {
                        continue;
                    }
                    auto* icon = slot.icon.Get();
                    if (!is_uobject_valid_fast(icon) || !is_uobject_valid_fast(slot.target.Get()) ||
                        display_object(icon) != display)
                    {
                        continue;
                    }
                    ColorTransform tinted = *incoming;
                    for (size_t i = 0; i < rgb_channel_count; ++i)
                    {
                        tinted.multiply[i] = 0;
                    }
                    tinted.add[0] = completed_marker_additive_color[0];
                    tinted.add[1] = completed_marker_additive_color[1];
                    tinted.add[2] = completed_marker_additive_color[2];
                    reinterpret_cast<SetColor>(original_color)(display, &tinted);
                    return;
                }
            }
            reinterpret_cast<SetColor>(original_color)(display, incoming);
        }
        void update(UObject* owner, void* native_slot, UObject* target)
        {
            const auto index = space::battle_marker_slot_index(
                reinterpret_cast<uintptr_t>(owner), reinterpret_cast<uintptr_t>(native_slot));
            if (index < 0 || index >= space_battle_marker_slot_count ||
                !is_uobject_valid_fast(owner) || !is_uobject_valid_fast(target))
            {
                return;
            }
            // These native helpers also display world guides. Only the exact
            // reflected enemy array of the real battle guide is eligible.
            if (guide.Get() != owner)
            {
                auto* p = owner->GetPropertyByNameInChain(L"m_EnemyGuide");
                if (!p || p->GetOffset_Internal() != enemy_guide_slots_offset ||
                    p->GetElementSize() != enemy_guide_slot_bytes ||
                    p->GetArrayDim() != space_battle_marker_slot_count)
                {
                    return;
                }
                for (auto& slot : slots)
                {
                    if (slot.green)
                    {
                        restore_color(slot.icon.Get(), slot.original);
                    }
                }
                guide = weak(owner);
                slots = {};
            }
            if (native_slot != reinterpret_cast<uint8_t*>(owner) + enemy_guide_slots_offset +
                                   index * enemy_guide_slot_bytes)
            {
                return;
            }
            if (guide.Get() != owner)
            {
                return;
            }
            auto* icon = *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(native_slot) +
                                                      enemy_guide_icon_offset);
            if (!is_uobject_valid_fast(icon))
            {
                return;
            }
            auto& cache = slots[index];
            const auto state = revision();
            const auto proximity =
                *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(native_slot) + 84);
            if (!cache.green && cache.target.Get() == target && cache.icon.Get() == icon &&
                cache.revision == state && cache.proximity == proximity)
            {
                return;
            }
            if (cache.icon.Get() != icon)
            {
                if (cache.green)
                {
                    restore_color(cache.icon.Get(), cache.original);
                }
                cache = {};
                cache.icon = weak(icon);
                cache.display_identity = reinterpret_cast<uintptr_t>(display_object(icon));
                cache.known = cache.icon.Get() == icon && transform(icon, cache.original, false);
            }
            if (!cache.known)
            {
                return;
            }
            if (cache.target.Get() != target)
            {
                cache.target = weak(target);
                cache.battle.clear();
                // KH3 passes a scene component for the encounter guide. Its owning
                // starter identifies the site; spawned enemy pawns expose the
                // corresponding starter through their own native accessor.
                for (auto* name : {L"GetCurrentEnemyBattleStarter", L"GetOwner"})
                {
                    auto* fn = target->GetFunctionByNameInChain(name);
                    if (is_uobject_valid_fast(fn) && fn->GetParmsSize() == sizeof(UObject*))
                    {
                        UObject* starter = nullptr;
                        target->ProcessEvent(fn, &starter);
                        if (is_uobject_valid_fast(starter))
                        {
                            cache.battle = space::battle_from_starter_path(starter->GetPathName());
                        }
                    }
                    if (!cache.battle.empty())
                    {
                        break;
                    }
                }
            }
            const bool green = !cache.battle.empty() && completed(cache.battle);
            if (!green && !cache.green)
            {
                cache.revision = state;
                cache.proximity = proximity;
                return;
            }
            ColorTransform current{};
            if (!transform(icon, current, false))
            {
                return;
            }
            ColorTransform color = current;
            for (size_t i = 0; i < rgb_channel_count; ++i)
            {
                color.multiply[i] = cache.original.multiply[i];
                color.add[i] = cache.original.add[i];
            }
            if (green)
            {
                color.multiply[0] = color.multiply[1] = color.multiply[2] = 0;
                color.add[0] = completed_marker_additive_color[0];
                color.add[1] = completed_marker_additive_color[1];
                color.add[2] = completed_marker_additive_color[2];
            }
            // Keep native alpha/fading and the separate battle-level stars intact.
            if (std::memcmp(&current, &color, sizeof(color)) == 0)
            {
                cache.green = green;
                cache.revision = state;
                cache.proximity = proximity;
            }
            else if (transform(icon, color, true))
            {
                cache.green = green;
                cache.revision = state;
                cache.proximity = proximity;
                ++updates;
            }
        }
    };
    RuntimeSpaceBattleMarkers::RuntimeSpaceBattleMarkers(Completed c, Revision r)
        : impl_(std::make_unique<Impl>(std::move(c), std::move(r)))
    {
    }
    RuntimeSpaceBattleMarkers::~RuntimeSpaceBattleMarkers() = default;
    bool RuntimeSpaceBattleMarkers::install()
    {
        auto& s = *impl_;
        if (s.front && s.behind && s.color)
        {
            return true;
        }
        const auto& p = space::native_profile();
        if (!p.supported || !color_writer_supported())
        {
            return false;
        }
        auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const uintptr_t front = p.steam ? 0x16b6720 : 0x1a28c80,
                        behind = p.steam ? 0x16b6c60 : 0x1a29800;

        constexpr std::array<uint8_t, 12> steam_front{0x48, 0x8b, 0xc4, 0x55, 0x41, 0x56,
                                                      0x41, 0x57, 0x48, 0x8d, 0x68, 0xb1};

        constexpr std::array<uint8_t, 12> egs_front{0x48, 0x8b, 0xc4, 0x55, 0x41, 0x56,
                                                    0x41, 0x57, 0x48, 0x8d, 0x68, 0xc8};

        constexpr std::array<uint8_t, 16> steam_behind{0x48, 0x8b, 0xc4, 0x55, 0x53, 0x56,
                                                       0x57, 0x41, 0x54, 0x41, 0x56, 0x41,
                                                       0x57, 0x48, 0x8d, 0x68};

        constexpr std::array<uint8_t, 16> egs_behind{0x48, 0x8b, 0xc4, 0x55, 0x57, 0x41,
                                                     0x54, 0x41, 0x56, 0x41, 0x57, 0x48,
                                                     0x8d, 0xa8, 0x28, 0xfe};
        const auto& fs = p.steam ? steam_front : egs_front;
        const auto& bs = p.steam ? steam_behind : egs_behind;
        if (std::memcmp(reinterpret_cast<void*>(base + front), fs.data(), fs.size()) ||
            std::memcmp(reinterpret_cast<void*>(base + behind), bs.data(), bs.size()) ||
            std::memcmp(reinterpret_cast<void*>(base + p.weak_assign_rva),
                        p.weak_assign_bytes.data(), p.weak_assign_bytes.size()))
        {
            return false;
        }
        Impl::self = &s;
        s.game_thread = GetCurrentThreadId();
        s.front = std::make_unique<PLH::x64Detour>(
            base + front, reinterpret_cast<uintptr_t>(&Impl::front_hook), &s.original_front);
        if (!s.front->hook() || !s.original_front)
        {
            s.front.reset();
            return false;
        }
        s.behind = std::make_unique<PLH::x64Detour>(
            base + behind, reinterpret_cast<uintptr_t>(&Impl::behind_hook), &s.original_behind);
        if (!s.behind->hook() || !s.original_behind)
        {
            s.behind.reset();
            s.front->unHook();
            s.front.reset();
            return false;
        }
        s.color = std::make_unique<PLH::x64Detour>(
            base + (p.steam ? steam_set_color_rva : egs_set_color_rva),
            reinterpret_cast<uintptr_t>(&Impl::color_hook), &original_color);
        if (!s.color->hook() || !original_color)
        {
            s.color.reset();
            s.behind->unHook();
            s.behind.reset();
            s.front->unHook();
            s.front.reset();
            return false;
        }
        RC::Output::send<RC::LogLevel::Warning>(
            L"[KH3APProbeCpp] Cleared space battle marker tint installed ({})\n",
            p.steam ? L"Steam" : L"EGS");
        return true;
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    void RuntimeSpaceBattleMarkers::inspect(std::ostream& out) const
    {
        const auto& s = *impl_;
        out << "{\"installed\":" << (s.front && s.behind && s.color ? "true" : "false")
            << ",\"updates\":" << s.updates << ",\"slots\":[";
        bool first = true;
        const GfxProbeFunctionPaths paths{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                          L"/Script/ScaleformUI.GFxObject:Invoke",
                                          L"/Script/ScaleformUI.GFxObject:SetMemberValue"};
        for (size_t i = 0; i < s.slots.size(); ++i)
        {
            const auto& slot = s.slots[i];
            auto* icon = slot.icon.Get();
            if (!is_uobject_valid_fast(icon))
            {
                continue;
            }
            if (!first)
            {
                out << ',';
            }
            first = false;
            ColorTransform color{};
            const bool read = transform(icon, color, false);
            auto* target = slot.target.Get();
            out << "{\"index\":" << i << ",\"target\":\""
                << json_escape(is_uobject_valid_fast(target) ? target->GetFullName() : L"")
                << "\",\"battle\":\"" << json_escape(slot.battle)
                << "\",\"green\":" << (slot.green ? "true" : "false") << ",\"icon_name\":\""
                << json_escape(try_gfx_get_member_value_string(paths, icon, L"name").value_or(L""))
                << "\",\"transform_read\":" << (read ? "true" : "false") << ",\"multiply\":[";
            for (size_t j = 0; j < color.multiply.size(); ++j)
            {
                if (j)
                {
                    out << ',';
                }
                out << color.multiply[j];
            }
            out << "],\"add\":[";
            for (size_t j = 0; j < color.multiply.size(); ++j)
            {
                if (j)
                {
                    out << ',';
                }
                out << color.add[j];
            }
            out << "]}";
        }
        out << "]}";
    }
#endif
} // namespace kh3ap
