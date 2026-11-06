#include <new>
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/movement/character_rails.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/FMemory.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <cmath>
#include <memory>
#include <stdexcept>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_rails
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto rail_property_cache_capacity = 32;
        constexpr auto max_rail_input_parameter_bytes = 32;

        constexpr auto max_rail_attack_row_bytes = 4096;
        constexpr auto max_rail_effect_height = 1000.0f;
        constexpr auto max_rail_effect_lifetime_seconds = 60.0f;
        ReflectedPropertyCache<rail_property_cache_capacity> properties;
        uint64_t original_blizzard_begin{};
        std::unique_ptr<PLH::x64Detour> blizzard_begin_hook;
        float rail_height{}, rail_life{};
        RuntimeOptionsInputOwnership shot_input;
        FWeakObjectPtr shot_pawn, shot_controller, shot_queue;
        bool supported_player(UObject* pawn);

        void release_shot()
        {
            auto* controller = shot_controller.Get();
            auto* pawn = shot_pawn.Get();
            shot_controller = {};
            shot_pawn = {};
            shot_queue = {};
            if (controller && supported_player(pawn) &&
                properties.read_object(controller, L"Pawn") == pawn)
            {
                runtime_controller_action_input(controller, L"attack", false);
            }
        }

        float* number(UObject* object, const wchar_t* name)
        {
            auto* field = properties.resolve(object, name);
            return field && field->GetClass().GetName() == L"FloatProperty" &&
                           field->GetElementSize() == sizeof(float)
                       ? field->ContainerPtrToValuePtr<float>(object)
                       : nullptr;
        }

        bool supported_player(UObject* pawn)
        {
            if (!character_swap::owns_swap_pawn(pawn))
            {
                return false;
            }
            auto* field = properties.resolve(pawn, L"m_PlayerUniqueID");
            if (!field || field->GetElementSize() != sizeof(uint8_t))
            {
                return false;
            }
            const auto player =
                static_cast<PlayerId>(*field->ContainerPtrToValuePtr<uint8_t>(pawn));
            return player == PlayerId::Aqua || player == PlayerId::Kairi ||
                   player == PlayerId::Roxas;
        }

        void begin_blizzard(UObject* projectile)
        {
            // Native BeginPlay decides whether to create the spline actor.
            // Shared defaults and AI casts remain untouched.
            auto* owner = properties.read_object(projectile, L"Owner");
            const FName unison_blizzard(L"TresProj_UnisonBlizzard_C", FNAME_Add);
            if (supported_player(owner) && character_traversal::rail_ready(owner) &&
                projectile->GetClassPrivate()->GetNamePrivate() != unison_blizzard &&
                kh02_actor_world(projectile) == kh02_actor_world(owner))
            {
                auto* life = number(projectile, L"m_GenerateRailLife");
                auto* height = number(projectile, L"m_GenerateRailHeight");
                auto* placement = properties.resolve(projectile, L"m_bCallPutEffectEventCall");
                // Alternate magic uses zero to disable its rail. Retain
                // authored nonzero settings and the original projectile.
                if (life && height && *life == 0.0f && placement && placement->IsA<FBoolProperty>())
                {
                    *life = rail_life;
                    if (*height == 0.0f)
                    {
                        *height = rail_height;
                    }
                    // Kairi disables the native distance-based placement
                    // callback as well as rail life. Keep its authored spacing.
                    static_cast<FBoolProperty*>(placement)->SetPropertyValueInContainer(projectile,
                                                                                        true);
                }
            }
            reinterpret_cast<void (*)(UObject*)>(original_blizzard_begin)(projectile);
        }

        void prepare_damage(UObject* pawn)
        {
            auto* source =
                find_loaded_runtime_asset(player_animation_support::sora_attack_data_path);
            auto* destination = properties.read_object(
                properties.read_object(properties.read_object(pawn, L"MyAtkColl"),
                                       L"m_pOwnerDataTableSet"),
                L"m_AttackData");
            constexpr auto schema = L"/Script/TresGame.TresAttackDataTable";
            Kh02DataTableView sora(source, schema, "character_rail_source_damage_layout");
            Kh02DataTableView alternate(destination, schema,
                                        "character_rail_destination_damage_layout");
            const auto bytes = sora.schema->GetStructureSize();
            if (sora.schema != alternate.schema || bytes < 1 || bytes > max_rail_attack_row_bytes)
            {
                throw std::runtime_error("character_rail_damage_schema");
            }
            constexpr auto name = L"FF_SLIDESHOT";
            if (alternate.find(name))
            {
                return;
            }
            auto* original = sora.find(name);
            if (!original)
            {
                throw std::runtime_error("character_rail_damage_missing");
            }
            auto* copy = FMemory::Malloc(bytes);
            if (!copy)
            {
                throw std::bad_alloc();
            }
            sora.schema->InitializeStruct(copy);
            sora.schema->CopyScriptStruct(copy, original);
            if (!alternate.insert(name, copy))
            {
                sora.schema->DestroyStruct(copy);
                FMemory::Free(copy);
                throw std::runtime_error("character_rail_damage_insert");
            }
        }

        struct ProjectileEntry
        {
            FName name;
            UClass* type;
        };
        static_assert(sizeof(ProjectileEntry) == 16);

        TArray<ProjectileEntry>* entries(UObject* set)
        {
            auto* field = properties.resolve(set, L"Projectiles");
            if (!field || !field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() !=
                    sizeof(ProjectileEntry))
            {
                throw std::runtime_error("character_rail_projectile_set_layout");
            }
            return field->ContainerPtrToValuePtr<TArray<ProjectileEntry>>(set);
        }
    } // namespace

    bool initialize()
    {
        if (blizzard_begin_hook)
        {
            return true;
        }
        if (!character_native::verify_sites({character_native::Site::blizzard_projectile_begin}))
        {
            return false;
        }
        auto* type = UObjectGlobals::StaticFindObject<UClass*>(
            nullptr, nullptr, L"/Script/TresGame.TresProj_MagicBlizzard");
        auto* defaults = is_uobject_valid_fast(type) ? type->GetClassDefaultObject() : nullptr;
        auto* height = number(defaults, L"m_GenerateRailHeight");
        auto* life = number(defaults, L"m_GenerateRailLife");
        if (!height || !life || !std::isfinite(*height) || !std::isfinite(*life) ||
            *height <= 0.0f || *height > max_rail_effect_height || *life <= 0.0f ||
            *life > max_rail_effect_lifetime_seconds)
        {
            return false;
        }
        rail_height = *height;
        rail_life = *life;
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        blizzard_begin_hook = std::make_unique<PLH::x64Detour>(
            base + character_native::rva(character_native::Site::blizzard_projectile_begin),
            reinterpret_cast<uintptr_t>(&begin_blizzard), &original_blizzard_begin);
        if (!blizzard_begin_hook->hook() || !original_blizzard_begin)
        {
            blizzard_begin_hook.reset();
            return false;
        }
        return true;
    }

    bool route(int controller_id, std::wstring_view key, int event, bool gamepad)
    {
        if (!gamepad || key != L"Gamepad_RightTrigger")
        {
            return false;
        }
        return shot_input.route(
            controller_id, std::wstring(key), event,
            [&]
            {
                if (event == 1)
                {
                    release_shot();
                    return true;
                }
                auto* pawn = character_swap::current_hero();
                if (!character_traversal::rail_ready(pawn))
                {
                    return false;
                }
                auto* queue = properties.read_object(pawn, L"MyStateComp");
                auto* state = properties.read_object(queue, L"m_pCurrentState");
                auto* id = properties.resolve(state, L"MyStateID");
                if (!id || id->GetElementSize() != sizeof(uint8_t) ||
                    *id->ContainerPtrToValuePtr<uint8_t>(state) != player_state_ids::rail_slide_mi)
                {
                    return false;
                }
                // R2 belongs to Blaster throughout this ride, including an entry
                // or paused phase. It must never fall through to character swap.
                if (!runtime_sora_control_ready(pawn))
                {
                    return true;
                }
                Kh02ReflectedCall paused(L"/Script/Engine.Default__GameplayStatics",
                                         L"IsGamePaused", "rail_input",
                                         max_rail_input_parameter_bytes);
                paused.object(L"WorldContextObject", pawn);
                paused.run();
                if (paused.boolean_result())
                {
                    return true;
                }
                auto* controller = properties.read_object(pawn, L"Controller");
                runtime_controller_action_input(controller, L"attack", true);
                shot_pawn = make_uobject_weak(pawn);
                shot_controller = make_uobject_weak(controller);
                shot_queue = make_uobject_weak(queue);
                return true;
            });
    }

    void observe_state_write(uintptr_t queue, uintptr_t state_address)
    {
        if (!shot_pawn.Get() || queue != reinterpret_cast<uintptr_t>(shot_queue.Get()))
        {
            return;
        }
        auto* state = reinterpret_cast<UObject*>(state_address);
        auto* id = properties.resolve(state, L"MyStateID");
        if (!id || id->GetElementSize() != sizeof(uint8_t) ||
            *id->ContainerPtrToValuePtr<uint8_t>(state) != player_state_ids::rail_slide_mi)
        {
            release_shot();
        }
    }

    void prepare(UObject* pawn)
    {
        if (!supported_player(pawn))
        {
            return;
        }
        prepare_damage(pawn);
        auto* equipment = properties.read_object(pawn, L"MyEquipment");
        auto* source = properties.read_object(equipment, L"m_ProjectileSet");
        auto* shot = find_loaded_runtime_asset_class(shot_projectile_path);
        const FName name(L"MI_SLIDESHOT", FNAME_Add);
        auto* source_entries = entries(source);
        for (const auto& entry : *source_entries)
        {
            if (entry.name == name && is_uobject_valid_fast(entry.type))
            {
                return;
            }
        }
        auto* field = properties.resolve(equipment, L"m_ProjectileSet");
        if (!shot || !field || field->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("character_rail_projectile_missing");
        }
        FStaticConstructObjectParameters parameters(source->GetClassPrivate(), equipment);
        parameters.SetFlags = RF_Transient;
        auto* private_set = UObjectGlobals::StaticConstructObject(parameters);
        if (!is_uobject_valid_fast(private_set))
        {
            throw std::runtime_error("character_rail_projectile_set_create");
        }
        auto* private_entries = entries(private_set);
        for (const auto& entry : *source_entries)
        {
            private_entries->Add(entry);
        }
        private_entries->Add({name, shot});
        *field->ContainerPtrToValuePtr<UObject*>(equipment) = private_set;
    }
} // namespace kh3ap::character_rails
