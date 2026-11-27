#include "kh3ap/features/player/characters/character_profile.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/combat/character_ground_slam.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace kh3ap::character_ground_slam
{
    using namespace RC::Unreal;
    namespace
    {
        using character_native::Site;
        using Setup = void (*)(UObject*);
        struct SetupHook
        {
            uint64_t original{};
            std::unique_ptr<PLH::x64Detour> detour;
        };
        // Each alternate overrides the same virtual attack setup. Each detour
        // must forward ordinary attacks to that particular class's original.
        template <Site site> SetupHook setup_hook;
        // EGS Attack state fields, verified against the forced dive-attack setup.
        constexpr size_t state_pawn_offset = 0x68;
        constexpr size_t active_attack_kind_offset = 0x1b0;
        constexpr size_t forced_attack_kind_offset = 0x1e8;
        constexpr int ground_slam_attack_kind = 53;

        constexpr size_t gimmick_damage_profile_offset = 0x311;
        constexpr size_t maximum_attack_row_bytes = 4096;
        constexpr auto asset_load_timeout = std::chrono::seconds(4);
        // Reserved latent asset-request IDs, not addresses or object offsets.
        constexpr int first_asset_request_id = 0x4b4833b0;
        constexpr size_t sora_animation_asset = 0;
        constexpr size_t sora_attack_data_asset = 1;
        constexpr auto slam_animation_name = L"DiveFallAttack";
        constexpr auto slam_damage_row_name = L"DIVEFALL_ATTACK";
        constexpr auto slam_finisher_damage_row_name = L"DIVEFALL_ATTACK_END";
        constexpr std::array damage_row_names{slam_damage_row_name, slam_finisher_damage_row_name};
        constexpr std::array asset_paths{player_animation_support::sora_animation_set_path,
                                         player_animation_support::sora_attack_data_path};
        FWeakObjectPtr prepared_pawn;
        FWeakObjectPtr prepared_animation_instance;
        FWeakObjectPtr requested_pawn;
        FWeakObjectPtr ground_slam_animation;
        std::chrono::steady_clock::time_point deadline{};
        FWeakObjectPtr unavailable_pawn;

        template <typename T> T read_native_field(UObject* value, size_t offset)
        {
            T result{};
            std::memcpy(&result, reinterpret_cast<std::byte*>(value) + offset, sizeof(result));
            return result;
        }

        UObject* object_property(UObject* value, const wchar_t* field)
        {
            return is_uobject_valid_fast(value)
                       ? try_read_reflected_object_property_value(value, field)
                       : nullptr;
        }

        UObject* owned_ground_slam()
        {
            auto* pawn = prepared_pawn.Get();
            if (!pawn || character_swap::current_hero() != pawn ||
                object_property(object_property(pawn, L"Controller"), L"Pawn") != pawn)
            {
                return nullptr;
            }
            auto* state =
                object_property(object_property(pawn, L"MyStateComp"), L"m_pCurrentState");
            if (!is_uobject_valid_fast(state))
            {
                return nullptr;
            }
            const auto type = state->GetClassPrivate()->GetName();
            if (type != L"TresPlayerState_AttackRI" && type != L"TresPlayerState_AttackAQ" &&
                type != L"TresPlayerState_AttackRO" && type != L"TresPlayerState_AttackKA")
            {
                return nullptr;
            }
            return read_native_field<int>(state, active_attack_kind_offset) ==
                           ground_slam_attack_kind
                       ? pawn
                       : nullptr;
        }

    } // namespace

    bool suppress_voice(UObject* notify, UObject* mesh, UObject* animation)
    {
        auto* pawn = prepared_pawn.Get();
        if (pawn && animation == ground_slam_animation.Get() &&
            character_swap::current_hero() == pawn && mesh == object_property(pawn, L"MyMesh") &&
            is_uobject_valid_fast(notify) && notify->GetOuterPrivate() == animation)
        {
            // The borrowed sequence has a hard-coded Sora battle voice.
            // Keep its hit/effect notifies and every other voice untouched.
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Alternate ground slam Sora voice suppressed\n"));
#endif
            return true;
        }
        return false;
    }

    namespace
    {

        template <Site site> void setup_attack(UObject* state)
        {
            auto* pawn = prepared_pawn.Get();
            // Character-specific setup can remap the forced dive to a normal
            // combo. Keep the original attack queue and exit for the slam.
            if (pawn && read_native_field<UObject*>(state, state_pawn_offset) == pawn &&
                prepared_animation_instance.Get() && character_swap::current_hero() == pawn &&
                object_property(object_property(pawn, L"Controller"), L"Pawn") == pawn &&
                (read_native_field<int>(state, forced_attack_kind_offset) ==
                     ground_slam_attack_kind ||
                 read_native_field<int>(state, active_attack_kind_offset) ==
                     ground_slam_attack_kind))
            {
                reinterpret_cast<Setup>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                                        character_native::rva(Site::sora_setup))(state);
                return;
            }
            reinterpret_cast<Setup>(setup_hook<site>.original)(state);
        }

        template <Site site> bool install_setup()
        {
            auto& hook = setup_hook<site>;
            if (hook.detour)
            {
                return true;
            }
            if (!character_native::verify_sites({site, Site::sora_setup}))
            {
                return false;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            auto detour = std::make_unique<PLH::x64Detour>(
                base + character_native::rva(site),
                reinterpret_cast<uintptr_t>(&setup_attack<site>), &hook.original);
            if (!detour->hook() || !hook.original)
            {
                return false;
            }
            hook.detour = std::move(detour);
            return true;
        }

        void add_damage_rows(UObject* source, UObject* destination)
        {
            constexpr auto schema_path = L"/Script/TresGame.TresAttackDataTable";
            Kh02DataTableView sora(source, schema_path, "character_ground_slam_source_layout");
            Kh02DataTableView alternate(destination, schema_path,
                                        "character_ground_slam_destination_layout");
            if (sora.schema != alternate.schema || sora.schema->GetStructureSize() < 1 ||
                sora.schema->GetStructureSize() > maximum_attack_row_bytes)
            {
                throw std::runtime_error("character_ground_slam_damage_schema");
            }
            for (const auto* name : damage_row_names)
            {
                if (alternate.find(name))
                {
                    continue;
                }
                auto* original = sora.find(name);
                if (!original)
                {
                    throw std::runtime_error("character_ground_slam_damage_missing");
                }
                auto* copy = FMemory::Malloc(sora.schema->GetStructureSize());
                if (!copy)
                {
                    throw std::bad_alloc();
                }
                sora.schema->InitializeStruct(copy);
                sora.schema->CopyScriptStruct(copy, original);
                // DataTable owns inserted rows: allocate with the engine heap,
                // deep-copy reflected fields, and let its ordinary GC/destructor
                // retain referenced assets and release the row. No disk edits.
                if (!alternate.insert(name, copy))
                {
                    sora.schema->DestroyStruct(copy);
                    FMemory::Free(copy);
                    throw std::runtime_error("character_ground_slam_damage_insert");
                }
            }
        }

    } // namespace

    void observe_gimmick_damage(UObject* component, UObject* controller, const void* damage_info,
                                UObject* causer)
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto* pawn = owned_ground_slam();
        FName attack_name{};
        if (damage_info)
        {
            std::memcpy(&attack_name, damage_info, sizeof(attack_name));
        }
        if (pawn && (causer == pawn || controller == object_property(pawn, L"Controller")))
        {
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Alternate ground slam damage event profile={} row={}\n"),
                static_cast<int>(
                    read_native_field<uint8_t>(component, gimmick_damage_profile_offset)),
                attack_name.ToString());
        }
#endif
    }

    bool initialize(int player_id)
    {
        if (!player_animation_support::supported_skeleton_layout() ||
            !player_animation_support::initialize_voice_notify())
        {
            return false;
        }
        switch (static_cast<PlayerId>(player_id))
        {
        case PlayerId::Riku:
        case PlayerId::RikuKH29:
            return install_setup<Site::riku_setup>();
        case PlayerId::Aqua:
            return install_setup<Site::aqua_setup>();
        case PlayerId::Roxas:
            return install_setup<Site::roxas_setup>();
        case PlayerId::Kairi:
            return install_setup<Site::kairi_setup>();
        default:
            return false;
        }
    }

    bool ready(UObject* pawn)
    {
        auto* animation_instance = prepared_animation_instance.Get();
        return is_uobject_valid_fast(pawn) && unavailable_pawn.Get() != pawn &&
               prepared_pawn.Get() == pawn && is_uobject_valid_fast(ground_slam_animation.Get()) &&
               is_uobject_valid_fast(animation_instance) &&
               animation_instance ==
                   object_property(object_property(pawn, L"MyMesh"), L"AnimScriptInstance");
    }

    bool prepare(UObject* pawn)
    {
        if (!is_uobject_valid_fast(pawn) || unavailable_pawn.Get() == pawn)
        {
            return true;
        }
        auto* id = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
        if (!id || id->GetElementSize() != sizeof(uint8_t) ||
            !is_alternate_keyblade_wielder(*id->ContainerPtrToValuePtr<uint8_t>(pawn)))
        {
            return true;
        }
        auto* animation_instance =
            object_property(object_property(pawn, L"MyMesh"), L"AnimScriptInstance");
        if (!is_uobject_valid_fast(animation_instance))
        {
            return false;
        }
        if (prepared_pawn.Get() == pawn && prepared_animation_instance.Get() == animation_instance)
        {
            return true;
        }
        std::array<UObject*, asset_paths.size()> assets{};
        bool ready = true;
        const bool first = requested_pawn.Get() != pawn;
        for (size_t index = 0; index < asset_paths.size(); ++index)
        {
            assets[index] = find_loaded_runtime_asset(asset_paths[index]);
            if (!is_uobject_valid_fast(assets[index]))
            {
                ready = false;
                if (first)
                {
                    request_runtime_asset_load(pawn, asset_paths[index], nullptr,
                                               first_asset_request_id + static_cast<int>(index));
                }
            }
        }
        if (first)
        {
            requested_pawn = make_uobject_weak(pawn);
            deadline = std::chrono::steady_clock::now() + asset_load_timeout;
        }
        if (!ready)
        {
            if (std::chrono::steady_clock::now() < deadline)
            {
                return false;
            }
            unavailable_pawn = make_uobject_weak(pawn);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Alternate ground slam assets timed out; preserving native "
                    "attack\n"));
            return true;
        }
        using player_animation_support::AnimationEntry;
        auto* entries = assets[sora_animation_asset]->GetPropertyByNameInChain(L"Anims");
        auto* sets = animation_instance->GetPropertyByNameInChain(L"MyAnimSets");
        if (!entries || entries->GetElementSize() != sizeof(TArray<AnimationEntry>) || !sets ||
            sets->GetElementSize() != sizeof(TArray<UObject*>) ||
            !initialize(*id->ContainerPtrToValuePtr<uint8_t>(pawn)))
        {
            unavailable_pawn = make_uobject_weak(pawn);
            return true;
        }
        UObject* ground_slam_sequence{};
        for (const auto& entry :
             *entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(assets[sora_animation_asset]))
        {
            if (entry.name.ToString() == slam_animation_name)
            {
                ground_slam_sequence = entry.asset;
                break;
            }
        }
        if (!is_uobject_valid_fast(ground_slam_sequence))
        {
            throw std::runtime_error("character_ground_slam_animation_missing");
        }
        auto* table = object_property(
            object_property(object_property(pawn, L"MyAtkColl"), L"m_pOwnerDataTableSet"),
            L"m_AttackData");
        if (!is_uobject_valid_fast(table))
        {
            return false;
        }
        add_damage_rows(assets[sora_attack_data_asset], table);
        auto& anim_sets = *sets->ContainerPtrToValuePtr<TArray<UObject*>>(animation_instance);
        bool already_bound = false;
        for (auto* set : anim_sets)
        {
            if (already_bound)
            {
                break;
            }
            if (is_uobject_valid_fast(set) && set->GetOuterPrivate() == animation_instance &&
                set->GetClassPrivate() == assets[sora_animation_asset]->GetClassPrivate())
            {
                for (const auto& entry :
                     *entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(set))
                {
                    if (entry.asset == ground_slam_sequence &&
                        entry.name.ToString() == slam_animation_name)
                    {
                        already_bound = true;
                        break;
                    }
                }
            }
        }
        if (!already_bound)
        {
            player_animation_support::allow_animation_skeleton(animation_instance,
                                                               ground_slam_sequence);
            FStaticConstructObjectParameters parameters(
                assets[sora_animation_asset]->GetClassPrivate(), animation_instance);
            parameters.SetFlags = RF_Transient;
            auto* private_set = UObjectGlobals::StaticConstructObject(parameters);
            if (!is_uobject_valid_fast(private_set))
            {
                throw std::runtime_error("character_ground_slam_animset_create");
            }
            entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(private_set)
                ->Add({FName(slam_animation_name, FNAME_Add), ground_slam_sequence});
            anim_sets.Add(private_set);
        }
        prepared_pawn = make_uobject_weak(pawn);
        prepared_animation_instance = make_uobject_weak(animation_instance);
        ground_slam_animation = make_uobject_weak(ground_slam_sequence);
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] Alternate ground slam ready pawn={}\n"), pawn->GetName());
        return true;
    }
} // namespace kh3ap::character_ground_slam
