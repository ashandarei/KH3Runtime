#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/encounter_repairs.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace kh3ap::enemy_remap_detail
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::wstring_view xigbar_class =
            L"/Game/Blueprints/Enemy/e_ex354/e_ex354_Pawn.e_ex354_Pawn_C";
        constexpr std::wstring_view xigbar_native_arena = L"/Game/Levels/rg_DLC/rg_13/";
        constexpr uintptr_t egs_change_field_rva = 0xcb0270;
        constexpr uintptr_t egs_receive_event_rva = 0xcb0050;
        constexpr uintptr_t egs_xigbar_tick_rva = 0xca8df0;
        constexpr uintptr_t egs_enter_cinematic_rva = 0x73a980;
        constexpr uintptr_t egs_pause_cinematic_ai_rva = 0x6aaf90;
        constexpr uintptr_t egs_clear_state_queue_rva = 0x1870de0;
        constexpr size_t field_id_offset = 0xe8c;
        constexpr size_t field_pending_offset = 0xe90;
        constexpr size_t cinematic_ai_paused_offset = 0x55c;
        constexpr int32_t field_event_stride = 0x38;
        constexpr std::wstring_view artema_warp_shot = L"e_ex354_Attack13_ArtemaWarpShot_DT_C";
        constexpr std::wstring_view cinematic_state = L"TresEnemyState_e_ex354_Cinematic";
        constexpr std::array<uint8_t, 32> egs_change_field_bytes{
            0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57,
            0x48, 0x83, 0xec, 0x20, 0x80, 0xb9, 0x90, 0x0e,
            0x00, 0x00, 0x00, 0x41, 0x0f, 0xb6, 0xf1, 0x41,
            0x0f, 0xb6, 0xe8, 0x8b, 0xfa, 0x48, 0x8b, 0xd9,
        };
        constexpr std::array<uint8_t, 32> egs_receive_event_bytes{
            0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0xc7,
            0x44, 0x24, 0x38, 0x00, 0x00, 0x00, 0x00, 0x48,
            0x8b, 0xd9, 0x48, 0x3b, 0x54, 0x24, 0x38, 0x0f,
            0x84, 0xe8, 0x01, 0x00, 0x00, 0x4c, 0x8b, 0x81,
        };
        constexpr std::array<uint8_t, 16> egs_xigbar_tick_bytes{
            0x48, 0x8b, 0xc4, 0x55, 0x48, 0x8d, 0x68, 0xa1,
            0x48, 0x81, 0xec, 0xd0, 0x00, 0x00, 0x00, 0x48,
        };
        constexpr std::array<uint8_t, 16> egs_enter_cinematic_bytes{
            0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b,
            0x99, 0xa0, 0x05, 0x00, 0x00, 0x48, 0x85, 0xdb,
        };
        constexpr std::array<uint8_t, 16> egs_pause_cinematic_ai_bytes{
            0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74,
            0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48,
        };
        constexpr std::array<uint8_t, 16> egs_clear_state_queue_bytes{
            0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0xb9,
            0x80, 0x02, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xd9,
        };

        using ChangeField = void (*)(UObject*, int32_t, bool, bool, bool);
        using XigbarTick = void (*)(UObject*, float);
        using CinematicOperation = void (*)(UObject*);
        using SetCinematicAIPause = void (*)(UObject*, bool);
        // This instance handler receives the FName as one eight-byte value.
        using ReceiveFieldEvent = void (*)(UObject*, uint64_t);
        uint64_t change_field_trampoline{};
        uint64_t xigbar_tick_trampoline{};
        ReceiveFieldEvent receive_field_event{};
        CinematicOperation enter_cinematic{};
        CinematicOperation clear_state_queue{};
        SetCinematicAIPause set_cinematic_ai_pause{};
        std::unique_ptr<PLH::x64Detour> change_field_hook;
        std::unique_ptr<PLH::x64Detour> xigbar_tick_hook;

        struct ArtemaHandoff
        {
            FWeakObjectPtr pawn;
            FWeakObjectPtr paused_controller;
            float elapsed{};
        };
        std::vector<ArtemaHandoff> artema_handoffs;

        auto state_is(UObject* queue, std::wstring_view class_name) -> bool
        {
            auto* state = read_boss_owner_property(queue, L"m_pCurrentState");
            return is_uobject_valid_fast(state) && state->GetClassPrivate()->GetName() == class_name;
        }

        void tick_xigbar(UObject* pawn, float delta_seconds)
        {
            reinterpret_cast<XigbarTick>(xigbar_tick_trampoline)(pawn, delta_seconds);
            if (artema_handoffs.empty())
            {
                return;
            }
            auto handoff = std::find_if(artema_handoffs.begin(), artema_handoffs.end(),
                                        [pawn](auto& entry) { return entry.pawn.Get() == pawn; });
            if (handoff == artema_handoffs.end())
            {
                return;
            }

            auto* queue = read_boss_owner_property(pawn, L"MyStateComp");
            auto* controller = read_boss_owner_property(pawn, L"Controller");
            auto* paused_controller = handoff->paused_controller.Get();
            if (paused_controller)
            {
                const bool entered = state_is(queue, cinematic_state);
                handoff->elapsed += std::max(0.0f, delta_seconds);
                if (!entered && state_is(queue, artema_warp_shot) && handoff->elapsed < 1.0f)
                {
                    return;
                }
                artema_handoffs.erase(handoff);
                if (entered)
                {
                    clear_state_queue(queue);
                }
                if (controller == paused_controller &&
                    read_boss_owner_property(controller, L"Pawn") == pawn)
                {
                    set_cinematic_ai_pause(controller, false);
                }
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Data Xigbar Artema handoff cinematic={}\n"),
                                              entered);
                return;
            }

            const auto* bytes = reinterpret_cast<const uint8_t*>(pawn);
            if (bytes[field_pending_offset] != 0)
            {
                handoff->elapsed += std::max(0.0f, delta_seconds);
                if (handoff->elapsed >= 1.0f)
                {
                    artema_handoffs.erase(handoff);
                }
                return;
            }
            const auto controller_owner = make_uobject_weak(controller);
            if (controller_owner.Get() != controller || !is_uobject_valid_fast(controller) ||
                read_boss_owner_property(controller, L"Pawn") != pawn ||
                reinterpret_cast<const uint8_t*>(controller)[cinematic_ai_paused_offset] != 0 ||
                !state_is(queue, artema_warp_shot))
            {
                artema_handoffs.erase(handoff);
                return;
            }

            // The arena ends Warp Shot by taking this pawn through cinematic
            // state. Run its native state and AI operations on the boss alone.
            handoff->paused_controller = controller_owner;
            handoff->elapsed = 0.0f;
            enter_cinematic(pawn);
            if (controller_owner.Get() == controller)
            {
                set_cinematic_ai_pause(controller, true);
            }
        }

        auto field_replies(UObject* pawn, int32_t field) -> std::optional<std::array<uint64_t, 2>>
        {
            auto* property = pawn->GetPropertyByNameInChain(L"m_FieldChangeRemoteArray");
            if (!property || !property->IsA<FArrayProperty>() || property->GetElementSize() != sizeof(ArrayHeader))
            {
                return std::nullopt;
            }
            auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(property)->GetInner());
            if (!inner || !inner->IsA<FStructProperty>() || inner->GetElementSize() != field_event_stride)
            {
                return std::nullopt;
            }
            auto* structure = static_cast<FStructProperty*>(inner)->GetStruct();
            if (!is_uobject_valid_fast(structure) || structure->GetName() != L"EX354_FieldChangeRemote")
            {
                return std::nullopt;
            }
            auto* id = find_struct_field_by_name(structure, L"FieldID");
            auto* move = find_struct_field_by_name(structure, L"XigbalMoveRemoteName");
            auto* resume = find_struct_field_by_name(structure, L"AIRestartRemoteName");
            if (!id || id->GetOffset_Internal() != 0 || id->GetElementSize() != sizeof(uint8_t) ||
                !move || !resume || move->GetClass().GetName() != L"NameProperty" ||
                resume->GetClass().GetName() != L"NameProperty" || move->GetOffset_Internal() != 0x20 ||
                resume->GetOffset_Internal() != 0x28 || move->GetElementSize() != sizeof(uint64_t) ||
                resume->GetElementSize() != sizeof(uint64_t))
            {
                return std::nullopt;
            }

            ArrayHeader events{};
            std::memcpy(&events, property->ContainerPtrToValuePtr<void>(pawn), sizeof(events));
            if (!events.data || events.num != 3 || events.max < events.num)
            {
                return std::nullopt;
            }
            const auto* records = reinterpret_cast<const uint8_t*>(events.data);
            for (int32_t index = 0; index < events.num; ++index)
            {
                const auto* record = records + index * field_event_stride;
                if (record[0] == field)
                {
                    std::array<uint64_t, 2> replies{};
                    std::memcpy(&replies[0], record + 0x20, sizeof(replies[0]));
                    std::memcpy(&replies[1], record + 0x28, sizeof(replies[1]));
                    if (replies[0] && replies[1] && replies[0] != replies[1])
                    {
                        return replies;
                    }
                    return std::nullopt;
                }
            }
            return std::nullopt;
        }

        void change_xigbar_field(UObject* pawn, int32_t field, bool immediate, bool no_cinematic,
                                 bool keep_position)
        {
            const auto original = reinterpret_cast<ChangeField>(change_field_trampoline);
            if (!s_runtime_enemy_remap.boss.boss_enabled.load() || !is_uobject_valid_fast(pawn) ||
                !has_runtime_boss_replacement_class(pawn) || pawn->GetClassPrivate()->GetPathName() != xigbar_class)
            {
                original(pawn, field, immediate, no_cinematic, keep_position);
                return;
            }

            auto binding = resolve_tracked_runtime_boss_pawn(pawn);
            if (!binding.level_entity)
            {
                binding = resolve_and_bind_runtime_boss_pawn(pawn);
            }
            const auto source = tracked_boss_source_class(binding.level_entity_path);
            if (!binding.level_entity || !source || *source == xigbar_class ||
                binding.level_entity_path.starts_with(xigbar_native_arena))
            {
                original(pawn, field, immediate, no_cinematic, keep_position);
                return;
            }

            const auto* bytes = reinterpret_cast<const uint8_t*>(pawn);
            const auto replies = bytes[field_pending_offset] == 0 ? field_replies(pawn, field) : std::nullopt;
            if (!replies)
            {
                original(pawn, field, immediate, no_cinematic, keep_position);
                return;
            }

            // Let the native state machine perform the mode change on the host
            // floor. Its keep-position option skips the native arena tag warp.
            const auto owner = make_uobject_weak(pawn);
            original(pawn, field, immediate, no_cinematic, true);
            int32_t selected_field{};
            if (owner.Get() != pawn || bytes[field_pending_offset] == 0)
            {
                return;
            }
            std::memcpy(&selected_field, bytes + field_id_offset, sizeof(selected_field));
            if (selected_field != field)
            {
                return;
            }
            receive_field_event(pawn, (*replies)[0]);
            receive_field_event(pawn, (*replies)[1]);
            if (no_cinematic && field == 0 &&
                state_is(read_boss_owner_property(pawn, L"MyStateComp"), artema_warp_shot))
            {
                std::erase_if(artema_handoffs, [](auto& entry) { return !entry.pawn.Get(); });
                if (std::none_of(artema_handoffs.begin(), artema_handoffs.end(),
                                 [pawn](auto& entry) { return entry.pawn.Get() == pawn; }))
                {
                    artema_handoffs.push_back({owner});
                }
            }
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Data Xigbar field change field={} "
                    "route=host_floor_replies entity={}\n"),
                field, binding.level_entity_path);
        }
    } // namespace

    void install_runtime_xigbar_field_support()
    {
        const auto& replacements = s_runtime_enemy_remap.boss.boss_class_replacements;
        if (change_field_hook || !std::any_of(replacements.begin(), replacements.end(), [](const auto& entry)
                                             { return entry.second == xigbar_class; }))
        {
            return;
        }

        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto address = base + egs_change_field_rva;
        const auto matches = [base](uintptr_t rva, const auto& bytes)
        {
            return std::memcmp(reinterpret_cast<const void*>(base + rva), bytes.data(), bytes.size()) == 0;
        };
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
            !native_build::egs.matches(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage) ||
            !matches(egs_change_field_rva, egs_change_field_bytes) ||
            !matches(egs_receive_event_rva, egs_receive_event_bytes) ||
            !matches(egs_xigbar_tick_rva, egs_xigbar_tick_bytes) ||
            !matches(egs_enter_cinematic_rva, egs_enter_cinematic_bytes) ||
            !matches(egs_pause_cinematic_ai_rva, egs_pause_cinematic_ai_bytes) ||
            !matches(egs_clear_state_queue_rva, egs_clear_state_queue_bytes))
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Data Xigbar field support unavailable "
                                                "reason=unrecognized_native_transition\n"));
            return;
        }

        receive_field_event = reinterpret_cast<ReceiveFieldEvent>(base + egs_receive_event_rva);
        enter_cinematic = reinterpret_cast<CinematicOperation>(base + egs_enter_cinematic_rva);
        clear_state_queue = reinterpret_cast<CinematicOperation>(base + egs_clear_state_queue_rva);
        set_cinematic_ai_pause = reinterpret_cast<SetCinematicAIPause>(base + egs_pause_cinematic_ai_rva);
        auto tick_hook = std::make_unique<PLH::x64Detour>(
            base + egs_xigbar_tick_rva, reinterpret_cast<uint64_t>(&tick_xigbar), &xigbar_tick_trampoline);
        if (!tick_hook->hook())
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Data Xigbar field support unavailable "
                                                "reason=artema_tick_hook_failed\n"));
            return;
        }
        auto hook = std::make_unique<PLH::x64Detour>(address, reinterpret_cast<uint64_t>(&change_xigbar_field),
                                                   &change_field_trampoline);
        if (hook->hook())
        {
            change_field_hook = std::move(hook);
            xigbar_tick_hook = std::move(tick_hook);
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Data Xigbar field support installed "
                                              "storefront=egs scope=tracked_replacement\n"));
        }
        else
        {
            tick_hook->unHook();
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Data Xigbar field support unavailable "
                                                "reason=field_hook_failed\n"));
        }
    }
} // namespace kh3ap::enemy_remap_detail
