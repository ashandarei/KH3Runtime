#include <chrono>
#include <utility>
#include <new>
#include <algorithm>
#include "kh3ap/features/player/characters/character_layout.h"
#include "kh3ap/features/player/characters/character_profile.h"
// PolyHook must precede Unreal headers to avoid their assembler macro names.
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/player/combat/character_shotlock.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/player/player_animation_support.h"
#include <Unreal/FMemory.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/utilities.h"
#include <nlohmann/json.hpp>
#endif
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_shotlock
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr std::array supported_diagnostic_scope_modes{0, 1, 6};
        constexpr auto max_shotlock_getter_parameter_bytes = 16;
        constexpr auto max_shotlock_pause_parameter_bytes = 32;
        constexpr auto race_input_before_request = 0;
        constexpr auto race_input_after_request = 1;
        constexpr auto race_input_after_factory = 2;

        constexpr auto shotlock_aim_input_hold = std::chrono::milliseconds(180);
        constexpr auto shotlock_aim_race_timeout = std::chrono::seconds(2);
        constexpr auto max_shotlock_aim_race_samples = 120;
        using player_animation_support::PlayerId;
        constexpr int riku_player_id = static_cast<int>(PlayerId::Riku);
        constexpr int roxas_player_id = static_cast<int>(PlayerId::Roxas);
        constexpr size_t maximum_attack_row_bytes = 4096;
        // EGS CL1065258: the ordinary shotlock selector reads these owning
        // controller fields. Oblivion maps to 41, then 42 at its 32-lock maximum.
        const uintptr_t select_shotlock_rva =
            character_native::rva(character_native::Site::shotlock_select);
        const uintptr_t roxas_update_rva =
            character_native::rva(character_native::Site::roxas_input_update);
        const uintptr_t shared_update_rva =
            character_native::rva(character_native::Site::shared_shotlock_update);
        const uintptr_t input_key_down_rva =
            character_native::rva(character_native::Site::input_key_down);
        constexpr size_t aim_flags_offset = 0xa08;
        constexpr uint32_t aim_requested_mask = 1u;
        constexpr size_t aim_pending_seconds_offset = 0xa0c;
        constexpr size_t aim_phase_offset = 0xa10;
        constexpr size_t hud_scope_offset = 0x3a8;
        constexpr size_t shotlock_capability_offset = 0xd80;
        constexpr size_t shotlock_capability_virtual_slot = 0x2938;
        const uintptr_t shotlock_capability_rva =
            character_native::rva(character_native::Site::shotlock_capability);
        constexpr uint32_t shotlock_capability_mask = 1u << 3;
        constexpr size_t override_kind_offset = 0xa34;
        constexpr size_t current_locks_offset = 0xa18;
        constexpr int oblivion_shotlock = 41;
        constexpr int oblivion_max_shotlock = 42;
        constexpr int oblivion_max_locks = 32;
        using SelectShotlock = void (*)(UObject*, bool);
        uint64_t original_select{};
        uint64_t original_update{};
        uint64_t original_shared_update{};
        std::unique_ptr<PLH::x64Detour> select_hook, update_hook, shared_update_hook;
        FWeakObjectPtr roxas_pawn, roxas_controller;
        bool release_pending{};
        struct SwapInputCheck
        {
            FWeakObjectPtr pawn, controller, world;
            bool active{}, observed{}, held{};
            uint32_t samples{};
        } swap_input_check;
        FWeakObjectPtr release_required_controller, release_required_world;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        int player_id(UObject* pawn);
        struct AimRaceTest
        {
            bool active{}, after_factory{}, pressed{}, released{};
            FWeakObjectPtr world, local, controller;
            std::chrono::steady_clock::time_point release_at{}, finish_at{};
            nlohmann::json result;
        } aim_race;

        nlohmann::json race_state(UObject* pawn)
        {
            auto result = snapshot(pawn);
            result.erase("race");
            result["player_id"] = player_id(pawn);
            return result;
        }
#endif
        constexpr std::array oblivion_damage_rows{L"SF_DFBURST", L"SF_LIGHTINCEPTION",
                                                  L"SF_LIGHTINCEPTION_BOMB",
                                                  L"SF_LIGHTINCEPTION_END"};
        constexpr std::array oblivion_firing_motions{L"M_1466", L"M_1467", L"M_1468", L"M_1469",
                                                     L"M_N481"};

        UObject* object(UObject* owner, const wchar_t* field)
        {
            return is_uobject_valid_fast(owner)
                       ? try_read_reflected_object_property_value(owner, field)
                       : nullptr;
        }

        int player_id(UObject* pawn)
        {
            auto* field = is_uobject_valid_fast(pawn)
                              ? pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID")
                              : nullptr;
            return field && field->GetElementSize() == 1
                       ? *field->ContainerPtrToValuePtr<uint8_t>(pawn)
                       : -1;
        }

        UObject* aim_controller(UObject* pawn)
        {
            auto* controller = object(pawn, L"Controller");
            return is_uobject_valid_fast(controller) && object(controller, L"Pawn") == pawn &&
                           controller->GetPropertyByNameInChain(L"m_CameraShootFlow") &&
                           character_native::verify_sites(
                               {character_native::Site::shared_shotlock_update})
                       ? controller
                       : nullptr;
        }

        UObject* active_camera(UObject* manager, const wchar_t* getter)
        {
            Kh02ReflectedCall call(manager, getter, "character_shotlock",
                                   max_shotlock_getter_parameter_bytes);
            call.run();
            return call.result();
        }

        UObject* cockpit(UObject* pawn)
        {
            return object(object(aim_controller(pawn), L"MyHUD"), L"TresCockpit");
        }

        UObject* hud_scope(UObject* pawn)
        {
            auto* target = object(cockpit(pawn), L"m_pHudTarget");
            return is_uobject_valid_fast(target)
                       ? *reinterpret_cast<UObject**>(reinterpret_cast<std::byte*>(target) +
                                                      hud_scope_offset)
                       : nullptr;
        }

        bool scope_blocks_swap(UObject* pawn)
        {
            if (!character_native::verify_sites({character_native::Site::aim_scope_mode}))
            {
                return true;
            }
            auto* scope = hud_scope(pawn);
            if (!is_uobject_valid_fast(scope))
            {
                return false;
            }
            constexpr GfxProbeFunctionPaths gfx{
                L"/Script/ScaleformUI.GFxObject:GetMemberValue", {}, {}};
            const auto label = try_gfx_get_member_value_string(gfx, scope, L"currentLabel");
            // Native aim reaches phase zero before the scope's out animation
            // finishes. Parking its controller in that gap can retain the
            // scope's circular filter on the cached character. Let the native
            // movie finish; inspect it only as part of a swap eligibility check.
            return !label || *label != L"off";
        }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        bool set_scope_mode(UObject* pawn, int mode)
        {
            auto* owner = cockpit(pawn);
            if (!is_uobject_valid_fast(owner))
            {
                return false;
            }
            if (!character_native::verify_sites({character_native::Site::aim_scope_mode}))
            {
                throw std::runtime_error("character_aim_scope_signature");
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            using SetScope = bool (*)(UObject*, int);
            return reinterpret_cast<SetScope>(
                base + character_native::rva(character_native::Site::aim_scope_mode))(owner, mode);
        }

        nlohmann::json scope_state(UObject* scope)
        {
            if (!is_uobject_valid_fast(scope))
            {
                return nullptr;
            }
            constexpr GfxProbeFunctionPaths gfx{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                                L"/Script/ScaleformUI.GFxObject:Invoke",
                                                {}};
            nlohmann::json result{{"object", to_utf8(scope->GetPathName())}};
            for (const auto* member : {L"name", L"visible", L"alpha", L"currentFrame",
                                       L"currentLabel", L"totalFrames", L"isPlaying"})
            {
                result[to_utf8(member)] = to_utf8(try_gfx_get_member_value(gfx, scope, member));
            }
            return result;
        }

        void press_race_input(UObject* pawn)
        {
            auto* controller = aim_controller(pawn);
            if (!controller)
            {
                throw std::runtime_error("character_aim_race_controller");
            }
            aim_race.controller = make_uobject_weak(controller);
            runtime_controller_action_input(controller, L"right_shoulder", true);
            aim_race.pressed = true;
            aim_race.release_at = std::chrono::steady_clock::now() + shotlock_aim_input_hold;
            aim_race.result["after_press_before_input_pass"] = race_state(pawn);
        }
#endif

        void observe_aim_input(UObject* controller, bool held)
        {
            // The native producer already runs for active player input. While
            // idle this is only a flag check; discovery is scoped to a swap.
            if (swap_input_check.active && controller == swap_input_check.controller.Get())
            {
                swap_input_check.observed = true;
                swap_input_check.held = held;
                ++swap_input_check.samples;
            }
        }

        void update_aim_input(UObject* controller, bool held, float delta)
        {
            const bool logical_request = held;
            if (character_swap::handoff_pending())
            {
                auto* local = object(controller, L"Player");
                auto* pawn = object(controller, L"Pawn");
                if (object(local, L"PlayerController") == controller &&
                    object(pawn, L"Controller") == controller &&
                    kh02_actor_world(pawn) == kh02_current_world())
                {
                    // Input can arrive after the factory commits but before
                    // the incoming camera/HUD is ready. Finish that handoff
                    // before accepting a new aim press.
                    if (held)
                    {
                        release_required_controller = make_uobject_weak(controller);
                        release_required_world = make_uobject_weak(kh02_current_world());
                    }
                    held = false;
                }
            }
            if (controller == release_required_controller.Get())
            {
                if (!logical_request || release_required_world.Get() != kh02_current_world())
                {
                    release_required_controller = {};
                    release_required_world = {};
                }
                else
                {
                    held = false;
                }
            }
            reinterpret_cast<void (*)(UObject*, bool, float)>(original_shared_update)(controller,
                                                                                      held, delta);
            observe_aim_input(controller, logical_request);
        }

        bool prepare_animations(UObject* pawn)
        {
            using player_animation_support::AnimationEntry;
            auto* instance = object(object(pawn, L"MyMesh"), L"AnimScriptInstance");
            auto* source =
                find_loaded_runtime_asset(player_animation_support::sora_animation_set_path);
            auto* maximum_source = find_loaded_runtime_asset(oblivion_max_animation_set_path);
            if (!instance || !source || !maximum_source)
            {
                return false;
            }
            auto* sets_field = instance->GetPropertyByNameInChain(L"MyAnimSets");
            if (!sets_field || !sets_field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(sets_field)->GetInner()->GetElementSize() !=
                    sizeof(UObject*))
            {
                throw std::runtime_error("character_shotlock_animsets_layout");
            }
            auto animations = [](UObject* set)
            {
                auto* field =
                    is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"Anims") : nullptr;
                if (!field || !field->IsA<FArrayProperty>() ||
                    static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() !=
                        sizeof(AnimationEntry))
                {
                    throw std::runtime_error("character_shotlock_anims_layout");
                }
                return field->ContainerPtrToValuePtr<TArray<AnimationEntry>>(set);
            };
            auto* sets = sets_field->ContainerPtrToValuePtr<TArray<UObject*>>(instance);
            std::vector<AnimationEntry> missing;
            for (const auto* motion : oblivion_firing_motions)
            {
                const FName name(motion, FNAME_Add);
                bool authored = false;
                for (auto* set : *sets)
                {
                    for (const auto& entry : *animations(set))
                    {
                        authored =
                            authored || (entry.name == name && is_uobject_valid_fast(entry.asset));
                    }
                }
                if (authored)
                {
                    continue;
                }
                UObject* asset{};
                for (auto* set : {source, maximum_source})
                {
                    for (const auto& entry : *animations(set))
                    {
                        if (entry.name == name && is_uobject_valid_fast(entry.asset))
                        {
                            asset = entry.asset;
                            break;
                        }
                    }
                }
                if (!asset)
                {
                    throw std::runtime_error("character_shotlock_firing_motion_missing");
                }
                player_animation_support::allow_animation_skeleton(instance, asset);
                missing.push_back({name, asset});
            }
            if (!missing.empty())
            {
                FStaticConstructObjectParameters parameters(source->GetClassPrivate(), instance);
                parameters.SetFlags = RF_Transient;
                auto* private_set = UObjectGlobals::StaticConstructObject(parameters);
                if (!is_uobject_valid_fast(private_set))
                {
                    throw std::runtime_error("character_shotlock_animset_create");
                }
                for (const auto& entry : missing)
                {
                    animations(private_set)->Add(entry);
                }
                sets->Add(private_set);
            }
            return true;
        }

        bool prepare_damage(UObject* pawn)
        {
            auto* source =
                find_loaded_runtime_asset(player_animation_support::sora_attack_data_path);
            auto* destination = object(object(object(pawn, L"MyAtkColl"), L"m_pOwnerDataTableSet"),
                                       L"m_AttackData");
            if (!source || !destination)
            {
                return false;
            }
            constexpr auto schema_path = L"/Script/TresGame.TresAttackDataTable";
            Kh02DataTableView sora(source, schema_path, "character_shotlock_source_layout");
            Kh02DataTableView roxas(destination, schema_path,
                                    "character_shotlock_destination_layout");
            if (sora.schema != roxas.schema || sora.schema->GetStructureSize() < 1 ||
                sora.schema->GetStructureSize() > maximum_attack_row_bytes)
            {
                throw std::runtime_error("character_shotlock_damage_schema");
            }
            for (const auto* name : oblivion_damage_rows)
            {
                if (roxas.find(name))
                {
                    continue;
                }
                auto* original = sora.find(name);
                if (!original)
                {
                    throw std::runtime_error("character_shotlock_damage_missing");
                }
                auto* copy = FMemory::Malloc(sora.schema->GetStructureSize());
                if (!copy)
                {
                    throw std::bad_alloc();
                }
                sora.schema->InitializeStruct(copy);
                sora.schema->CopyScriptStruct(copy, original);
                // Native collision/projectiles resolve rows through their owner's
                // table set. Roxas omits these rows entirely. Preserve Sora's
                // magic flag and power coefficients; the engine applies the
                // equipped Magic total and target defenses as usual. Inserting
                // only missing rows retains every authored Roxas attack.
                if (!roxas.insert(name, copy))
                {
                    sora.schema->DestroyStruct(copy);
                    FMemory::Free(copy);
                    throw std::runtime_error("character_shotlock_damage_insert");
                }
            }
            return true;
        }

        bool prepare_projectiles(UObject* pawn)
        {
            auto* equipment = object(pawn, L"MyEquipment");
            auto* source = object(equipment, L"m_ProjectileSet");
            auto* partial = find_loaded_runtime_asset_class(oblivion_projectile_class_path);
            auto* maximum = find_loaded_runtime_asset_class(oblivion_max_projectile_class_path);
            if (!source || !partial || !maximum)
            {
                return false;
            }
            struct ProjectileEntry
            {
                FName name;
                UClass* type;
            };
            static_assert(sizeof(ProjectileEntry) == 16);
            auto* entries_field = source->GetPropertyByNameInChain(L"Projectiles");
            auto* set_field = equipment->GetPropertyByNameInChain(L"m_ProjectileSet");
            if (!entries_field || !entries_field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(entries_field)->GetInner()->GetElementSize() !=
                    sizeof(ProjectileEntry) ||
                !set_field || set_field->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("character_shotlock_projectile_set_layout");
            }
            const std::array required{
                ProjectileEntry{FName(L"DF_BURST", FNAME_Add), partial},
                ProjectileEntry{FName(L"SF_LIGHTINCEPTION", FNAME_Add), maximum}};
            auto* original_entries =
                entries_field->ContainerPtrToValuePtr<TArray<ProjectileEntry>>(source);
            std::vector<ProjectileEntry> missing;
            for (const auto& required_entry : required)
            {
                bool authored = false;
                for (const auto& entry : *original_entries)
                {
                    authored = authored || (entry.name == required_entry.name &&
                                            is_uobject_valid_fast(entry.type));
                }
                if (!authored)
                {
                    missing.push_back(required_entry);
                }
            }
            if (!missing.empty())
            {
                FStaticConstructObjectParameters parameters(source->GetClassPrivate(), equipment);
                parameters.SetFlags = RF_Transient;
                auto* private_set = UObjectGlobals::StaticConstructObject(parameters);
                if (!is_uobject_valid_fast(private_set))
                {
                    throw std::runtime_error("character_shotlock_projectile_set_create");
                }
                auto* entries =
                    entries_field->ContainerPtrToValuePtr<TArray<ProjectileEntry>>(private_set);
                for (const auto& entry : *original_entries)
                {
                    entries->Add(entry);
                }
                for (const auto& entry : missing)
                {
                    entries->Add(entry);
                }
                // Keep Cross Shoot and every authored Roxas mapping. The native
                // shotlock state spawns and targets the added classes normally.
                *set_field->ContainerPtrToValuePtr<UObject*>(equipment) = private_set;
            }
            return true;
        }

        void update_roxas(UObject* controller, float delta)
        {
            reinterpret_cast<void (*)(UObject*, float)>(original_update)(controller, delta);
            auto* pawn = roxas_pawn.Get();
            if (!pawn || controller != roxas_controller.Get() ||
                !character_swap::owns_swap_pawn(pawn) || object(controller, L"Pawn") != pawn)
            {
                return;
            }
            struct Key
            {
                FName name;
                uintptr_t details{}, shared{};
            };
            static const FName shoulder_name(L"Gamepad_RightShoulder", FNAME_Add);
            Key key{shoulder_name};
            static_assert(sizeof(Key) == 24);
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            // Match Aqua/Riku's native IsInputKeyDown call, including the
            // engine-owned destruction of its by-value FKey temporary.
            const bool held = reinterpret_cast<bool (*)(UObject*, Key*)>(base + input_key_down_rva)(
                controller, &key);
            const int phase = *reinterpret_cast<int*>(reinterpret_cast<std::byte*>(controller) +
                                                      aim_phase_offset);
            if (phase == 0 && character_traversal::ready(pawn) && runtime_sora_control_ready(pawn))
            {
                // Roxas omits Aqua/Kairi's held-jump Glide dispatch entirely.
                // Use their native request, which retains its ability, Jump
                // phase and pending-state checks, rather than forcing state 4.
                static const FName jump_name(L"Gamepad_FaceButton_Right", FNAME_Add);
                Key jump_key{jump_name};
                if (reinterpret_cast<bool (*)(UObject*, Key*)>(base + input_key_down_rva)(
                        controller, &jump_key))
                {
                    const auto heading = (*reinterpret_cast<uintptr_t**>(
                        pawn))[character_native::layout::glide_heading_vtable_offset /
                               sizeof(uintptr_t)];
                    const float direction = reinterpret_cast<float (*)(UObject*)>(heading)(pawn);
                    const float duration = *reinterpret_cast<float*>(
                        reinterpret_cast<std::byte*>(controller) +
                        character_native::layout::controller_glide_duration_offset);
                    reinterpret_cast<bool (*)(void*, float, float)>(
                        base + character_native::rva(character_native::Site::glide_request))(
                        reinterpret_cast<std::byte*>(pawn) +
                            character_native::layout::player_state_interface_offset,
                        direction, duration);
                }
            }
            if (held || phase != 0 || release_pending)
            {
                reinterpret_cast<void (*)(UObject*, bool, float)>(base + shared_update_rva)(
                    controller, held, delta);
                // Leaving aim sets the native release/cooldown latch. Its
                // ordinary idle branch clears that latch on the following
                // released frame; stop this bounded tail only after that pass.
                release_pending = held || phase != 0;
            }
            else
            {
                // Roxas omits the shared producer on idle/released frames.
                // Drain a new possession's release gate on that first neutral
                // frame too, so his first fresh aim press is not swallowed.
                if (controller == release_required_controller.Get())
                {
                    release_required_controller = {};
                    release_required_world = {};
                }
                observe_aim_input(controller, false);
            }
        }

        void select_shotlock(UObject* controller, bool refresh)
        {
            auto* pawn = object(controller, L"Pawn");
            auto original = reinterpret_cast<SelectShotlock>(original_select);
            if (!character_swap::owns_swap_pawn(pawn) || player_id(pawn) != roxas_player_id ||
                object(pawn, L"Controller") != controller)
            {
                original(controller, refresh);
                return;
            }
            auto* override_kind = reinterpret_cast<int*>(reinterpret_cast<std::byte*>(controller) +
                                                         override_kind_offset);
            const auto saved = *override_kind;
            const auto locks = *reinterpret_cast<int*>(reinterpret_cast<std::byte*>(controller) +
                                                       current_locks_offset);
            *override_kind =
                locks >= oblivion_max_locks ? oblivion_max_shotlock : oblivion_shotlock;
            // Retain Focus Aspir, normal target eligibility, lock counts, HUD
            // updates and focus consumption in the original selector/state.
            original(controller, refresh);
            *override_kind = saved;
        }

        bool install()
        {
            if (select_hook && update_hook && shared_update_hook)
            {
                return true;
            }
            if (!player_animation_support::supported_skeleton_layout())
            {
                return false;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!character_native::verify_sites({character_native::Site::shotlock_select,
                                                 character_native::Site::roxas_input_update,
                                                 character_native::Site::shared_shotlock_update,
                                                 character_native::Site::input_key_down,
                                                 character_native::Site::shotlock_capability}))
            {
                return false;
            }
            select_hook = std::make_unique<PLH::x64Detour>(
                base + select_shotlock_rva, reinterpret_cast<uintptr_t>(&select_shotlock),
                &original_select);
            update_hook = std::make_unique<PLH::x64Detour>(
                base + roxas_update_rva, reinterpret_cast<uintptr_t>(&update_roxas),
                &original_update);
            shared_update_hook = std::make_unique<PLH::x64Detour>(
                base + shared_update_rva, reinterpret_cast<uintptr_t>(&update_aim_input),
                &original_shared_update);
            if (!select_hook->hook() || !original_select || !update_hook->hook() ||
                !original_update || !shared_update_hook->hook() || !original_shared_update)
            {
                shared_update_hook.reset();
                update_hook.reset();
                select_hook.reset();
                return false;
            }
            return true;
        }
    } // namespace

    bool initialize()
    {
        return install();
    }

    bool blocks_swap(UObject* pawn)
    {
        auto* controller = aim_controller(pawn);
        if (!controller)
        {
            return true;
        }
        if (swap_input_check.active && swap_input_check.observed && swap_input_check.held &&
            controller == swap_input_check.controller.Get())
        {
            return true;
        }
        const auto* bytes = reinterpret_cast<const std::byte*>(controller);
        const int phase = *reinterpret_cast<const int*>(bytes + aim_phase_offset);
        const auto flags = *reinterpret_cast<const uint32_t*>(bytes + aim_flags_offset);
        const float pending_seconds =
            *reinterpret_cast<const float*>(bytes + aim_pending_seconds_offset);
        // Native aim accumulates its hold delay while phase is still zero.
        // Parking that controller would retain the unfinished request. Read
        // the native request/timer, independently of any input binding.
        if (phase != 0 || (flags & aim_requested_mask) != 0 || pending_seconds != 0.0f)
        {
            return true;
        }
        auto* aim_camera = object(controller, L"m_CameraShootFlow");
        if (is_uobject_valid_fast(aim_camera))
        {
            auto* manager = object(controller, L"PlayerCameraManager");
            if (!is_uobject_valid_fast(manager))
            {
                return true;
            }
            // Camera removal/blending can finish after the native phase clears.
            for (const auto* getter : {L"GetActiveCamera", L"GetActiveViewCamera"})
            {
                if (active_camera(manager, getter) == aim_camera)
                {
                    return true;
                }
            }
        }
        return scope_blocks_swap(pawn);
    }

    void finish_swap_input_check()
    {
        swap_input_check = {};
    }

    void prepare_incoming_input(UObject* pawn)
    {
        if (player_id(pawn) == static_cast<int>(PlayerId::KingMickey))
        {
            release_required_controller = {};
            release_required_world = {};
            return;
        }
        if (auto* controller = aim_controller(pawn))
        {
            // A new possession has not sampled its input yet. An already-held
            // logical aim request belongs to the handoff until it is released.
            release_required_controller = make_uobject_weak(controller);
            release_required_world = make_uobject_weak(kh02_actor_world(pawn));
        }
    }

    bool begin_swap_input_check(UObject* pawn)
    {
        finish_swap_input_check();
        // Mickey has no aim input producer; returning from his trap retains
        // the existing safety checks without waiting for an absent callback.
        if (player_id(pawn) == static_cast<int>(PlayerId::KingMickey))
        {
            return true;
        }
        if (!install())
        {
            return false;
        }
        auto* controller = aim_controller(pawn);
        auto* world = kh02_actor_world(pawn);
        if (!controller || !is_uobject_valid_fast(world) || world != kh02_current_world())
        {
            return false;
        }
        swap_input_check.pawn = make_uobject_weak(pawn);
        swap_input_check.controller = make_uobject_weak(controller);
        swap_input_check.world = make_uobject_weak(world);
        swap_input_check.active = true;
        return true;
    }

    SwapInputStatus swap_input_status(UObject* pawn)
    {
        if (!swap_input_check.active)
        {
            return SwapInputStatus::Clear;
        }
        if (pawn != swap_input_check.pawn.Get() ||
            aim_controller(pawn) != swap_input_check.controller.Get() ||
            kh02_current_world() != swap_input_check.world.Get())
        {
            return SwapInputStatus::Blocked;
        }
        if (!swap_input_check.observed)
        {
            return SwapInputStatus::Waiting;
        }
        return swap_input_check.held ? SwapInputStatus::Blocked : SwapInputStatus::Clear;
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    nlohmann::json snapshot(UObject* pawn)
    {
        nlohmann::json result{{"blocks_swap", blocks_swap(pawn)}};
        result["input_check"] = {{"active", swap_input_check.active},
                                 {"observed", swap_input_check.observed},
                                 {"held", swap_input_check.held},
                                 {"samples", swap_input_check.samples}};
        result["release_required"] =
            release_required_controller.Get() == object(pawn, L"Controller");
        result["flow_scope"] = scope_state(hud_scope(pawn));
        auto* controller = aim_controller(pawn);
        if (!controller)
        {
            return result;
        }
        const auto* bytes = reinterpret_cast<const std::byte*>(controller);
        result["phase"] = *reinterpret_cast<const int*>(bytes + aim_phase_offset);
        result["flags"] = *reinterpret_cast<const uint32_t*>(bytes + aim_flags_offset);
        result["pending_seconds"] =
            *reinterpret_cast<const float*>(bytes + aim_pending_seconds_offset);
        auto* aim_camera = object(controller, L"m_CameraShootFlow");
        auto name = [](UObject* value)
        {
            return is_uobject_valid_fast(value) ? to_utf8(value->GetPathName()) : std::string{};
        };
        result["aim_camera"] = name(aim_camera);
        if (auto* manager = object(controller, L"PlayerCameraManager");
            is_uobject_valid_fast(manager))
        {
            auto* active = active_camera(manager, L"GetActiveCamera");
            auto* view = active_camera(manager, L"GetActiveViewCamera");
            result["active_camera"] = name(active);
            result["view_camera"] = name(view);
            result["aim_camera_active"] =
                aim_camera && (aim_camera == active || aim_camera == view);
        }
        auto* target_hud =
            object(object(object(controller, L"MyHUD"), L"TresCockpit"), L"m_pHudTarget");
        if (auto* field = is_uobject_valid_fast(target_hud)
                              ? target_hud->GetPropertyByNameInChain(L"m_targetInfoAry")
                              : nullptr)
        {
            result["hud_targets"] =
                field->ContainerPtrToValuePtr<TArray<std::byte>>(target_hud)->Num();
        }
        result["race"] = aim_race.result;
        return result;
    }

    nlohmann::json diagnostic_scope_mode(UObject* pawn, int mode)
    {
        auto* controller = aim_controller(pawn);
        if ((std::find(supported_diagnostic_scope_modes.begin(),
                       supported_diagnostic_scope_modes.end(),
                       mode) == supported_diagnostic_scope_modes.end()) ||
            !controller || !runtime_sora_control_ready(pawn) || character_swap::handoff_pending())
        {
            throw std::runtime_error("character_aim_scope_requires_idle_player");
        }
        const auto* bytes = reinterpret_cast<const std::byte*>(controller);
        if (*reinterpret_cast<const int*>(bytes + aim_phase_offset) != 0 ||
            *reinterpret_cast<const float*>(bytes + aim_pending_seconds_offset) != 0.0f)
        {
            throw std::runtime_error("character_aim_scope_requires_idle_aim");
        }
        const auto previous = make_uobject_weak(hud_scope(pawn));
        auto before = scope_state(previous.Get());
        const bool applied = set_scope_mode(pawn, mode);
        return {{"applied", applied},
                {"before", std::move(before)},
                {"previous_after", scope_state(previous.Get())},
                {"after", scope_state(hud_scope(pawn))}};
    }

    bool diagnostic_race_start(UObject* pawn, int order)
    {
        if (aim_race.active || order < race_input_before_request ||
            order > race_input_after_factory || !character_swap::swap_available(pawn) ||
            character_swap::handoff_pending())
        {
            return false;
        }
        const auto before = race_state(pawn);
        aim_race = {};
        aim_race.active = true;
        aim_race.world = make_uobject_weak(kh02_actor_world(pawn));
        aim_race.local = make_uobject_weak(object(object(pawn, L"Controller"), L"Player"));
        aim_race.finish_at = std::chrono::steady_clock::now() + shotlock_aim_race_timeout;
        aim_race.result = {{"status", "running"},
                           {"order", order},
                           {"before", before},
                           {"target", character_swap::swap_target(pawn)},
                           {"samples", nlohmann::json::array()}};
        aim_race.after_factory = order == race_input_after_factory;
        if (order == race_input_before_request)
        {
            press_race_input(pawn);
        }
        aim_race.result["request_accepted"] =
            character_swap::route(0, L"Gamepad_RightTrigger", 0, true);
        character_swap::route(0, L"Gamepad_RightTrigger", 1, true);
        if (order == race_input_after_request)
        {
            press_race_input(pawn);
        }
        return true;
    }

    void diagnostic_after_factory(UObject* pawn)
    {
        if (aim_race.active && aim_race.after_factory &&
            kh02_actor_world(pawn) == aim_race.world.Get())
        {
            aim_race.after_factory = false;
            press_race_input(pawn);
        }
    }

    void diagnostic_race_tick(std::chrono::steady_clock::time_point now)
    {
        if (!aim_race.active)
        {
            return;
        }
        auto* controller = object(aim_race.local.Get(), L"PlayerController");
        auto* pawn = object(controller, L"Pawn");
        if (!is_uobject_valid_fast(pawn) || kh02_current_world() != aim_race.world.Get())
        {
            if (aim_race.pressed && !aim_race.released && aim_race.controller.Get())
            {
                try
                {
                    runtime_controller_action_input(aim_race.controller.Get(), L"right_shoulder",
                                                    false);
                    aim_race.released = true;
                }
                catch (const std::exception& failure)
                {
                    aim_race.result["release_error"] = failure.what();
                }
            }
            aim_race.active = false;
            aim_race.result["status"] = "canceled_context";
            aim_race.result["input_released"] = aim_race.released;
            return;
        }
        if (aim_race.pressed && !aim_race.released && now >= aim_race.release_at)
        {
            runtime_controller_action_input(aim_race.controller.Get(), L"right_shoulder", false);
            aim_race.released = true;
        }
        auto state = race_state(pawn);
        if (aim_race.result["samples"].size() < max_shotlock_aim_race_samples)
        {
            aim_race.result["samples"].push_back(state);
        }
        if (now >= aim_race.finish_at)
        {
            aim_race.active = false;
            aim_race.result["status"] = "completed";
            aim_race.result["input_released"] = aim_race.released;
            aim_race.result["after"] = std::move(state);
        }
    }
#endif

    bool prepare(UObject* pawn)
    {
        if (!character_swap::owns_swap_pawn(pawn))
        {
            return true;
        }
        const auto kind = player_id(pawn);
        if (kind != riku_player_id && kind != roxas_player_id)
        {
            return true;
        }
        if (!install())
        {
            return false;
        }
        if (kind == riku_player_id)
        {
            Kh02ReflectedCall enable(pawn, L"BP_SetEnableShootLockModeForRIKU",
                                     "character_shotlock", max_shotlock_pause_parameter_bytes);
            enable.boolean(L"IsOn", true);
            enable.run();
        }
        else
        {
            if (!character_native::verify_sites({character_native::Site::glide_request}))
            {
                throw std::runtime_error("character_glide_request_signature");
            }
            if (!prepare_damage(pawn) || !prepare_animations(pawn) || !prepare_projectiles(pawn))
            {
                return false;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto getter = (*reinterpret_cast<uintptr_t**>(
                pawn))[shotlock_capability_virtual_slot / sizeof(uintptr_t)];
            if (getter != base + shotlock_capability_rva ||
                !character_native::verify_sites({character_native::Site::shotlock_capability}))
            {
                throw std::runtime_error("character_shotlock_capability_layout");
            }
            // Roxas has no authored shotlock switch. Enable the same per-pawn
            // capability as Riku/Aqua without changing any shared class default.
            auto* flags = reinterpret_cast<uint32_t*>(reinterpret_cast<std::byte*>(pawn) +
                                                      shotlock_capability_offset);
            *flags |= shotlock_capability_mask;
            roxas_pawn = make_uobject_weak(pawn);
            roxas_controller = make_uobject_weak(object(pawn, L"Controller"));
            release_pending = false;
        }
        return true;
    }
} // namespace kh3ap::character_shotlock
