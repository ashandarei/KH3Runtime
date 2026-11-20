#include "kh3ap/features/enemies/bosses/encounter_repairs.h"
#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/master_xehanort_queries.h"
#include "kh3ap/features/enemies/bosses/xemnas_center.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/platform/game_build.h"

#include "kh3ap/core/reflection/reflected_function_call.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <Windows.h>

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        using Position = std::array<float, 3>;
        constexpr auto king_class = L"/Game/Blueprints/Enemy/e_ex711/e_ex711_Pawn.e_ex711_Pawn_C";
        constexpr size_t movement_center_offset = 0xef8;

        bool supported_movement_center(bool king)
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!base)
            {
                return false;
            }
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return false;
            }
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE)
            {
                return false;
            }
            // The phase-transition Rush reads this private FVector on the body pawn.
            // Check the XY and Z loads on each retail image before touching the instance.
            constexpr uint8_t egs_read[] = {0xf2, 0x0f, 0x10, 0x87, 0xf8, 0x0e, 0x00,
                                            0x00, 0x8b, 0x87, 0x00, 0x0f, 0x00, 0x00};
            constexpr uint8_t steam_read[] = {0x41, 0x0f, 0x10, 0x86, 0xf8, 0x0e, 0x00, 0x00, 0x0f,
                                              0x57, 0xdb, 0x41, 0x8b, 0x86, 0x00, 0x0f, 0x00, 0x00};
            const auto build = native_build::identify(nt->FileHeader.TimeDateStamp,
                                                      nt->OptionalHeader.SizeOfImage);
            if (!king)
            {
                // GetCenterLocation copies the complete XIII pawn cache into its FVector result.
                constexpr uint8_t getter[] = {0xf2, 0x0f, 0x10, 0x81, 0x80, 0x0c, 0x00, 0x00,
                                              0x8b, 0x81, 0x88, 0x0c, 0x00, 0x00, 0xf2, 0x41,
                                              0x0f, 0x11, 0x00, 0x41, 0x89, 0x40, 0x08, 0xc3};
                if (build == native_build::Build::Unsupported)
                {
                    return false;
                }
                const auto rva = build == native_build::Build::Egs ? 0x24c54e5 : 0x20dff05;
                return !std::memcmp(reinterpret_cast<const void*>(base + rva), getter,
                                    sizeof(getter));
            }
            switch (build)
            {
            case native_build::Build::Egs:
                return !std::memcmp(reinterpret_cast<const void*>(base + 0xe6c82f), egs_read,
                                    sizeof(egs_read));
            case native_build::Build::Steam:
                return !std::memcmp(reinterpret_cast<const void*>(base + 0xc1171a), steam_read,
                                    sizeof(steam_read));
            default:
                return false;
            }
        }

        void initialize_ansem_blackboard(UObject* pawn, const Position& center)
        {
            auto* board = read_boss_owner_property(read_boss_owner_property(pawn, L"Controller"),
                                                   L"Blackboard");
            auto* asset = read_boss_owner_property(board, L"BlackboardAsset");
            if (!asset ||
                asset->GetPathName() != L"/Game/Blueprints/Enemy/e_ex352/e_ex352_BB.e_ex352_BB")
            {
                return;
            }
            try
            {
                ReflectedFunctionCall set(board, L"SetValueAsVector", "Ansem arena center", 64);
                set.name(L"KeyName", L"CenterLocation");
                set.vector(L"VectorValue", center[0], center[1], center[2]);
                set.run();
                ReflectedFunctionCall get(board, L"GetValueAsVector", "Ansem arena center", 64);
                get.name(L"KeyName", L"CenterLocation");
                get.run();
                if (get.result_vector() == center)
                {
                    Output::send<LogLevel::Normal>(
                        STR("[KH3APProbeCpp] Data Ansem blackboard center verified ({},{},{})\n"),
                        center[0], center[1], center[2]);
                }
            }
            catch (const std::exception&)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Data Ansem blackboard center unavailable\n"));
            }
        }

        bool is_center_marker(UObject* marker, UObject* entity)
        {
            if (!is_uobject_valid_fast(marker) ||
                !is_uobject_a_fast(marker, AActor::StaticClass()) ||
                !marker->GetName().starts_with(L"CenterActor"))
            {
                return false;
            }
            auto* world = actor_owning_world(entity);
            return world && actor_owning_world(marker) == world;
        }
    } // namespace

    void initialize_replacement_movement_center(UObject* pawn,
                                                const RuntimeBossEntityBinding& binding)
    {
        constexpr auto ansem_class = L"/Game/Blueprints/Enemy/e_ex352/e_ex352_Pawn.e_ex352_Pawn_C";
        constexpr auto yozora_class = L"/Game/Blueprints/Enemy/e_ex781/e_ex781_Pawn.e_ex781_Pawn_C";
        constexpr auto vanitas_class =
            L"/Game/Blueprints/Enemy/e_ex360/e_ex360_Pawn.e_ex360_Pawn_C";
        constexpr auto xion_class = L"/Game/Blueprints/Enemy/e_ex359/e_ex359_Pawn.e_ex359_Pawn_C";
        const auto* klass = pawn->GetClassPrivate();
        const auto path = klass->GetPathName();
        const bool king = path == king_class;
        if (!king && path != ansem_class && path != yozora_class && path != vanitas_class &&
            path != xion_class)
        {
            return;
        }
        const auto source = tracked_boss_source_class(binding.level_entity_path);
        if (!source || *source == path)
        {
            return;
        }
        if (path == yozora_class)
        {
            activate_yozora_query_contexts();
            install_yozora_keyblade_steal();
            preload_yozora_rooftop(pawn);
        }
        if (path == vanitas_class)
        {
            activate_vanitas_query_contexts();
        }

        static const bool king_layout = supported_movement_center(true);
        static const bool xiii_layout = supported_movement_center(false);
        const auto offset = king ? movement_center_offset : size_t{0xc80};
        if (!(king ? king_layout : xiii_layout) ||
            klass->GetPropertiesSize() < offset + sizeof(Position))
        {
            return;
        }
        auto* storage = reinterpret_cast<std::byte*>(pawn) + offset;
        if (!king)
        {
            const auto current = read_boss_arena_center(pawn);
            // The XIII native initializer can already supply a correct host center.
            // Preserve it, including its floor adjustment, whenever it is populated.
            if (!current || std::memcmp(storage, current->data(), sizeof(Position)))
            {
                return;
            }
            if (*current != Position{})
            {
                if (path == ansem_class)
                {
                    initialize_ansem_blackboard(pawn, *current);
                }
                return;
            }
        }
        auto* entity = binding.level_entity;
        auto** user_object = data_xemnas_center_slot(entity);
        auto* marker = user_object ? *user_object : nullptr;
        if (!is_center_marker(marker, entity))
        {
            // Some arenas place CenterActor in the enemy level without binding it as user data.
            auto* level = entity->GetOuterPrivate();
            marker = find_loaded_runtime_asset(level->GetPathName() + L".CenterActor");
            if (!is_uobject_valid_fast(marker) || marker->GetOuterPrivate() != level ||
                !is_center_marker(marker, entity))
            {
                marker = nullptr;
            }
        }
        Position center{};
        if (marker)
        {
            const auto location = static_cast<AActor*>(marker)->K2_GetActorLocation();
            center = {static_cast<float>(location.X()), static_cast<float>(location.Y()),
                      static_cast<float>(location.Z())};
        }
        else
        {
            // An unmarked encounter still supplies the original boss's world-space spawn.
            auto* spawn = entity->GetPropertyByNameInChain(L"m_SpawnLocation");
            if (!spawn || !spawn->IsA<FStructProperty>() ||
                spawn->GetElementSize() != sizeof(center))
            {
                return;
            }
            auto* type = static_cast<FStructProperty*>(spawn)->GetStruct();
            if (!is_uobject_valid_fast(type) || type->GetName() != L"Vector")
            {
                return;
            }
            std::memcpy(center.data(), spawn->ContainerPtrToValuePtr<void>(entity), sizeof(center));
        }
        if (!std::all_of(center.begin(), center.end(),
                         [](float v)
                         {
                             return std::isfinite(v) && std::abs(v) <= 10000000.f;
                         }))
        {
            return;
        }
        std::memcpy(storage, center.data(), sizeof(center));
        if (path == ansem_class)
        {
            initialize_ansem_blackboard(pawn, center);
        }
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] {} movement center=({},{},{}) source={} entity={}\n"),
            klass->GetName(), center[0], center[1], center[2],
            marker ? STR("CenterActor") : STR("host_spawn"), binding.level_entity_path);
    }
} // namespace kh3ap::enemy_remap_detail
