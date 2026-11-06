#include "kh3ap/features/player/characters/character_profile.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/combat/kairi_ground_slam.h"
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

namespace kh3ap::kairi_ground_slam
{
    using namespace RC::Unreal;
    namespace
    {
        // EGS CL1065258 attack setup and animation-notify entry points.
        // Each hook below also checks its exact instruction prologue.
        const uintptr_t kairi_setup_rva = character_native::rva(character_native::Site::kairi_setup);
        const uintptr_t sora_setup_rva = character_native::rva(character_native::Site::sora_setup);
        const uintptr_t voice_notify_rva = character_native::rva(character_native::Site::voice_notify);
        const uintptr_t attack_begin_rva = character_native::rva(character_native::Site::attack_begin);
        const uintptr_t attack_end_rva = character_native::rva(character_native::Site::attack_end);
        // EGS Attack state fields, verified against the forced dive-attack setup.
        constexpr size_t state_pawn_offset = 0x68;
        constexpr size_t active_attack_kind_offset = 0x1b0;
        constexpr size_t forced_attack_kind_offset = 0x1e8;
        constexpr int ground_slam_attack_kind = 53;

        // TresAnimNotifyState_Attack fields identify the collision body and damage row.
        constexpr size_t notify_collision_group_offset = 0x48;
        constexpr size_t notify_damage_row_offset = 0x50;
        constexpr size_t gimmick_damage_profile_offset = 0x311;
        constexpr size_t maximum_attack_row_bytes = 4096;
        constexpr auto asset_load_timeout = std::chrono::seconds(4);
        // Reserved latent asset-request IDs, not addresses or object offsets.
        constexpr int first_asset_request_id = 0x4b4833b0;
        constexpr size_t sora_animation_asset = 0;
        constexpr size_t sora_attack_data_asset = 1;
        constexpr size_t kairi_attack_data_asset = 2;
        constexpr auto slam_animation_name = L"DiveFallAttack";
        constexpr auto slam_damage_row_name = L"DIVEFALL_ATTACK";
        constexpr auto slam_finisher_damage_row_name = L"DIVEFALL_ATTACK_END";
        constexpr std::array damage_row_names{slam_damage_row_name, slam_finisher_damage_row_name};
        constexpr std::array asset_paths{
            player_animation_support::sora_animation_set_path,
            player_animation_support::sora_attack_data_path,
            player_animation_support::kairi_attack_data_path};
        using Setup = void (*)(UObject*);
        using VoiceNotify = void (*)(UObject*, UObject*, UObject*);
        using AttackBegin = void (*)(UObject*, UObject*, UObject*, float);
        uint64_t original_setup{};
        uint64_t original_voice_notify{};
        uint64_t original_attack_begin{};
        uint64_t original_attack_end{};
        std::unique_ptr<PLH::x64Detour> setup_hook;
        std::unique_ptr<PLH::x64Detour> voice_notify_hook;
        std::unique_ptr<PLH::x64Detour> attack_begin_hook;
        std::unique_ptr<PLH::x64Detour> attack_end_hook;
        FWeakObjectPtr prepared_pawn;
        FWeakObjectPtr prepared_animation_instance;
        FWeakObjectPtr requested_pawn;
        FWeakObjectPtr ground_slam_animation;
        FName slam_damage_row;
        FName slam_finisher_damage_row;
        FName sora_sphere_collision_group;
        FName kairi_keyblade_collision_group;
        std::chrono::steady_clock::time_point deadline{};
        bool unavailable{};

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
            return is_uobject_valid_fast(state) &&
                           state->GetClassPrivate()->GetName() == L"TresPlayerState_AttackKA" &&
                           read_native_field<int>(state, active_attack_kind_offset) ==
                               ground_slam_attack_kind
                       ? pawn
                       : nullptr;
        }

        bool redirect_finisher(UObject* notify, UObject* mesh, UObject* animation)
        {
            auto* pawn = prepared_pawn.Get();
            return pawn && character_swap::current_hero() == pawn &&
                   animation == ground_slam_animation.Get() &&
                   mesh == object_property(pawn, L"MyMesh") && is_uobject_valid_fast(notify) &&
                   notify->GetOuterPrivate() == animation &&
                   read_native_field<FName>(notify, notify_collision_group_offset) ==
                       sora_sphere_collision_group &&
                   read_native_field<FName>(notify, notify_damage_row_offset) ==
                       slam_finisher_damage_row;
        }

        void begin_attack(UObject* notify, UObject* mesh, UObject* animation, float duration)
        {
            const bool redirect = redirect_finisher(notify, mesh, animation);
            auto* group = reinterpret_cast<FName*>(reinterpret_cast<std::byte*>(notify) +
                                                   notify_collision_group_offset);
            const auto original_group = *group;
            if (redirect)
            {
                *group = kairi_keyblade_collision_group;
            }
            reinterpret_cast<AttackBegin>(original_attack_begin)(notify, mesh, animation, duration);
            if (redirect)
            {
                *group = original_group;
            }
        }

        void end_attack(UObject* notify, UObject* mesh, UObject* animation)
        {
            const bool redirect = redirect_finisher(notify, mesh, animation);
            auto* group = reinterpret_cast<FName*>(reinterpret_cast<std::byte*>(notify) +
                                                   notify_collision_group_offset);
            const auto original_group = *group;
            if (redirect)
            {
                *group = kairi_keyblade_collision_group;
            }
            reinterpret_cast<VoiceNotify>(original_attack_end)(notify, mesh, animation);
            if (redirect)
            {
                *group = original_group;
            }
        }

        void play_voice(UObject* notify, UObject* mesh, UObject* animation)
        {
            auto* pawn = prepared_pawn.Get();
            if (pawn && character_swap::current_hero() == pawn &&
                animation == ground_slam_animation.Get() &&
                mesh == object_property(pawn, L"MyMesh") && is_uobject_valid_fast(notify) &&
                notify->GetOuterPrivate() == animation)
            {
                // The borrowed sequence has a hard-coded Sora battle voice.
                // Keep its hit/effect notifies and every other voice untouched.
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Kairi ground slam Sora voice suppressed\n"));
#endif
                return;
            }
            reinterpret_cast<VoiceNotify>(original_voice_notify)(notify, mesh, animation);
        }

        void setup_attack(UObject* state)
        {
            auto* pawn = prepared_pawn.Get();
            // Native Kairi setup explicitly remaps forced kind 53 to her combo
            // kind 234. Retain Sora's setup only for this prepared player's ground slam;
            // the original queue, attack state, hit notifications and exit run.
            if (pawn && prepared_animation_instance.Get() &&
                character_swap::current_hero() == pawn &&
                read_native_field<UObject*>(state, state_pawn_offset) == pawn &&
                object_property(object_property(pawn, L"Controller"), L"Pawn") == pawn &&
                (read_native_field<int>(state, forced_attack_kind_offset) ==
                     ground_slam_attack_kind ||
                 read_native_field<int>(state, active_attack_kind_offset) ==
                     ground_slam_attack_kind))
            {
                reinterpret_cast<Setup>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                                        sora_setup_rva)(state);
                return;
            }
            reinterpret_cast<Setup>(original_setup)(state);
        }

        bool install()
        {
            if (setup_hook && voice_notify_hook && attack_begin_hook &&
                attack_end_hook)
            {
                return true;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!player_animation_support::supported_skeleton_layout() || !character_native::verify_sites({character_native::Site::kairi_setup, character_native::Site::sora_setup, character_native::Site::voice_notify, character_native::Site::attack_begin, character_native::Site::attack_end}))
            {
                return false;
            }
            setup_hook = std::make_unique<PLH::x64Detour>(
                base + kairi_setup_rva, reinterpret_cast<uintptr_t>(&setup_attack),
                &original_setup);
            voice_notify_hook = std::make_unique<PLH::x64Detour>(
                base + voice_notify_rva, reinterpret_cast<uintptr_t>(&play_voice),
                &original_voice_notify);
            attack_begin_hook = std::make_unique<PLH::x64Detour>(
                base + attack_begin_rva, reinterpret_cast<uintptr_t>(&begin_attack),
                &original_attack_begin);
            attack_end_hook = std::make_unique<PLH::x64Detour>(
                base + attack_end_rva, reinterpret_cast<uintptr_t>(&end_attack),
                &original_attack_end);
            if (!setup_hook->hook() || !original_setup || !voice_notify_hook->hook() || !original_voice_notify ||
                !attack_begin_hook->hook() || !original_attack_begin || !attack_end_hook->hook() ||
                !original_attack_end)
            {
                attack_end_hook.reset();
                attack_begin_hook.reset();
                voice_notify_hook.reset();
                setup_hook.reset();
                return false;
            }
            slam_damage_row = FName(slam_damage_row_name, FNAME_Add);
            slam_finisher_damage_row = FName(slam_finisher_damage_row_name, FNAME_Add);
            sora_sphere_collision_group = FName(L"SphereM", FNAME_Add);
            kairi_keyblade_collision_group = FName(L"KB_StickM", FNAME_Add);
            return true;
        }

        void add_damage_rows(UObject* source, UObject* destination)
        {
            constexpr auto schema_path = L"/Script/TresGame.TresAttackDataTable";
            Kh02DataTableView sora(source, schema_path, "kairi_ground_slam_source_layout");
            Kh02DataTableView kairi(destination, schema_path,
                                    "kairi_ground_slam_destination_layout");
            if (sora.schema != kairi.schema || sora.schema->GetStructureSize() < 1 ||
                sora.schema->GetStructureSize() > maximum_attack_row_bytes)
            {
                throw std::runtime_error("kairi_ground_slam_damage_schema");
            }
            for (const auto* name : damage_row_names)
            {
                if (kairi.find(name))
                {
                    continue;
                }
                auto* original = sora.find(name);
                if (!original)
                {
                    throw std::runtime_error("kairi_ground_slam_damage_missing");
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
                if (!kairi.insert(name, copy))
                {
                    sora.schema->DestroyStruct(copy);
                    FMemory::Free(copy);
                    throw std::runtime_error("kairi_ground_slam_damage_insert");
                }
            }
        }

    } // namespace

    void observe_gimmick_damage(UObject* component, UObject* controller,
                               const void* damage_info, UObject* causer)
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
                STR("[KH3APProbeCpp] Kairi ground slam damage event profile={} row={}\n"),
                static_cast<int>(
                    read_native_field<uint8_t>(component, gimmick_damage_profile_offset)),
                attack_name.ToString());
        }
#endif
    }

    bool initialize()
    {
        return !unavailable && install();
    }

    bool prepare(UObject* pawn)
    {
        if (unavailable || !is_uobject_valid_fast(pawn))
        {
            return true;
        }
        auto* id = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
        if (!id || id->GetElementSize() != sizeof(uint8_t) ||
            *id->ContainerPtrToValuePtr<uint8_t>(pawn) !=
                static_cast<uint8_t>(player_animation_support::PlayerId::Kairi))
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
            unavailable = true;
            RC::Output::send<RC::LogLevel::Warning>(STR(
                "[KH3APProbeCpp] Kairi ground slam assets timed out; preserving native attack\n"));
            return true;
        }
        using player_animation_support::AnimationEntry;
        auto* entries = assets[sora_animation_asset]->GetPropertyByNameInChain(L"Anims");
        auto* sets = animation_instance->GetPropertyByNameInChain(L"MyAnimSets");
        if (!entries || entries->GetElementSize() != sizeof(TArray<AnimationEntry>) || !sets ||
            sets->GetElementSize() != sizeof(TArray<UObject*>) || !initialize())
        {
            unavailable = true;
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
            throw std::runtime_error("kairi_ground_slam_animation_missing");
        }
        player_animation_support::allow_animation_skeleton(animation_instance,
                                                           ground_slam_sequence);
        FStaticConstructObjectParameters parameters(assets[sora_animation_asset]->GetClassPrivate(),
                                                    animation_instance);
        parameters.SetFlags = RF_Transient;
        auto* private_set = UObjectGlobals::StaticConstructObject(parameters);
        if (!is_uobject_valid_fast(private_set))
        {
            throw std::runtime_error("kairi_ground_slam_animset_create");
        }
        entries->ContainerPtrToValuePtr<TArray<AnimationEntry>>(private_set)
            ->Add({FName(slam_animation_name, FNAME_Add), ground_slam_sequence});
        add_damage_rows(assets[sora_attack_data_asset], assets[kairi_attack_data_asset]);
        // Add only the missing animation to this ABP. All existing Kairi
        // animation names, combos and the shared Sora asset stay untouched.
        sets->ContainerPtrToValuePtr<TArray<UObject*>>(animation_instance)->Add(private_set);
        prepared_pawn = make_uobject_weak(pawn);
        prepared_animation_instance = make_uobject_weak(animation_instance);
        ground_slam_animation = make_uobject_weak(ground_slam_sequence);
        RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Kairi ground slam ready\n"));
        return true;
    }
} // namespace kh3ap::kairi_ground_slam
