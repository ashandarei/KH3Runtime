#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/encounter_repairs.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Windows.h>
#include <cstring>
#include <algorithm>

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        using Position = std::array<float, 3>;
        constexpr auto yozora = L"/Game/Blueprints/Enemy/e_ex781/e_ex781_Pawn.e_ex781_Pawn_C";
        constexpr auto high_voltage =
            L"/Game/Blueprints/Enemy/e_ex781/BT/e_ex781_BT_HighVoltage.e_ex781_BT_HighVoltage";
        using EnterService = void (*)(UObject*, UObject*, void*);
        std::atomic<EnterService> original_enter{};
        std::unique_ptr<RuntimeBossHpLimitOverride> entry_override;

        Position* high_voltage_literal(UObject* node)
        {
            auto* tree = read_boss_owner_property(node, L"TreeAsset");
            if (!tree || tree->GetPathName() != high_voltage)
            {
                return nullptr;
            }
            // The only vector service in this tree sets Destination = (0,0,0)
            // on entry. Its numeric literal is copied synchronously to this AI's board.
            auto* field = node->GetPropertyByNameInChain(L"m_InParamList");
            if (!field || !field->IsA<FArrayProperty>())
            {
                return nullptr;
            }
            auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(field)->GetInner());
            if (!inner || !inner->IsA<FStructProperty>())
            {
                return nullptr;
            }
            auto* structure = static_cast<FStructProperty*>(inner)->GetStruct();
            if (!is_uobject_valid_fast(structure))
            {
                return nullptr;
            }
            auto* value = find_struct_field_by_name(structure, L"Value");
            if (!value || !value->IsA<FStructProperty>() ||
                value->GetElementSize() != sizeof(Position))
            {
                return nullptr;
            }
            auto* type = static_cast<FStructProperty*>(value)->GetStruct();
            if (!is_uobject_valid_fast(type) || type->GetName() != L"Vector")
            {
                return nullptr;
            }
            auto* list = field->ContainerPtrToValuePtr<FScriptArray>(node);
            if (!list || list->Num() != 1 || !list->GetData())
            {
                return nullptr;
            }
            return value->ContainerPtrToValuePtr<Position>(list->GetData());
        }

        void enter_service(UObject* node, UObject* component, void* memory)
        {
            const auto enter = original_enter.load(std::memory_order_acquire);
            auto* literal = high_voltage_literal(node);
            if (literal && *literal == Position{})
            {
                auto* controller = read_boss_owner_property(component, L"AIOwner");
                auto* pawn = read_boss_owner_property(controller, L"Pawn");
                if (pawn && pawn->GetClassPrivate()->GetPathName() == yozora &&
                    read_boss_owner_property(pawn, L"Controller") == controller)
                {
                    auto binding = resolve_tracked_runtime_boss_pawn(pawn);
                    auto source = tracked_boss_source_class(binding.level_entity_path);
                    const auto center = read_boss_arena_center(pawn);
                    if (source && *source != yozora && center)
                    {
                        // Game-thread BT service activation. Restore the shared template
                        // immediately after it has populated the executing controller's board.
                        struct Restore
                        {
                            Position& slot;
                            Position previous;
                            ~Restore()
                            {
                                slot = previous;
                            }
                        } restore{*literal, *literal};
                        *literal = *center;
                        enter(node, component, memory);
                        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Yozora High Voltage "
                                                           "destination=({},{},{}) entity={}\n"),
                                                       (*center)[0], (*center)[1], (*center)[2],
                                                       binding.level_entity_path);
                        return;
                    }
                }
            }
            enter(node, component, memory);
        }
    } // namespace

    bool release_yozora_climax_camera(UObject* camera)
    {
        // The wait state also drives the five-second windup. Leave it running and
        // release only its own camera from the local camera manager's active stack.
        auto* manager = read_boss_owner_property(camera, L"m_CameraManager");
        if (!manager)
        {
            return false;
        }
        auto& tracking = s_runtime_enemy_remap.boss.tracking;
        bool owned = false;
        {
            std::lock_guard lock{tracking.boss_state_mutex};
            for (const auto& [path, weak] : tracking.boss_bound_actors)
            {
                auto* pawn = weak.Get();
                if (is_uobject_valid_fast(pawn) && pawn->GetClassPrivate()->GetPathName() == yozora &&
                    read_boss_owner_property(pawn, L"m_ClimaxSceneCamera") == camera)
                {
                    const auto source = tracking.remapped_boss_source_classes.find(path);
                    auto* state = read_boss_owner_property(
                        read_boss_owner_property(pawn, L"MyStateComp"), L"m_pCurrentState");
                    owned = source != tracking.remapped_boss_source_classes.end() && source->second != yozora &&
                            state && state->GetClassPrivate()->GetName() == L"e_ex781_Attack2_HVClimaxWaitCamera_T_C";
                    break;
                }
            }
        }
        if (!owned)
        {
            return false;
        }
        try
        {
            Kh02ReflectedCall active{manager, L"GetActiveCamera", "yozora_climax_camera", 32};
            active.run();
            if (active.result() != camera)
            {
                return false;
            }
            Kh02ReflectedCall end{camera, L"EndTresCamera", "yozora_climax_camera", 128};
            end.boolean(L"bTakeover", false);
            end.run();
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Released Yozora climax shot camera={}\n"),
                                           camera->GetPathName());
        }
        catch (const std::exception&)
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Yozora climax camera release unavailable\n"));
        }
        return true;
    }

    void install_yozora_high_voltage_center()
    {
        const auto& replacements = s_runtime_enemy_remap.boss.boss_class_replacements;
        if (entry_override || std::none_of(replacements.begin(), replacements.end(),
                                           [](const auto& pair)
                                           {
                                               return pair.second == yozora &&
                                                      pair.first != pair.second;
                                           }))
        {
            return;
        }
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresBTService_BlackboardVectorValueModifierInOut");
        auto* defaults = is_uobject_valid_fast(klass) ? klass->GetClassDefaultObject() : nullptr;
        if (!is_uobject_valid_fast(defaults))
        {
            return;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!base)
        {
            return;
        }
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return;
        }
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return;
        }
        const auto build =
            native_build::identify(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        if (build == native_build::Build::Unsupported)
        {
            return;
        }
        const bool egs = build == native_build::Build::Egs;
        // Adjacent OnBecomeRelevant / OnCeaseRelevant iterate the reflected
        // input / output arrays respectively on both retail builds.
        auto** slot = *reinterpret_cast<void***>(defaults) + 89;
        if (reinterpret_cast<uintptr_t>(slot[0]) != base + (egs ? 0x606880 : 0x48b5c0) ||
            reinterpret_cast<uintptr_t>(slot[1]) != base + (egs ? 0x606b30 : 0x48bc90))
        {
            return;
        }
        auto guard = std::make_unique<RuntimeBossHpLimitOverride>();
        guard->slot = slot;
        guard->original = *slot;
        guard->replacement = reinterpret_cast<void*>(&enter_service);
        PLH::MemAccessor memory{};
        PLH::MemoryProtector protect{reinterpret_cast<uint64_t>(slot), sizeof(void*),
                                     PLH::ProtFlag::R | PLH::ProtFlag::W, memory};
        if (!protect.isGood())
        {
            return;
        }
        original_enter.store(reinterpret_cast<EnterService>(*slot), std::memory_order_release);
        *slot = guard->replacement;
        entry_override = std::move(guard);
    }
} // namespace kh3ap::enemy_remap_detail
