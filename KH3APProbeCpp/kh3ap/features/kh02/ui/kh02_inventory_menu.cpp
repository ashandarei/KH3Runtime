#include "kh02_inventory_menu.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "alternate_weapon_catalog.h"
#include "character_record_lookup_stub.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/rewards/kh02_item_slots.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <algorithm>
#include <cstring>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    struct Kh02InventoryMenu::Impl
    {
        using BuildRoster = void (*)(UObject*);
        using TickCamp = void (*)(UObject*);
        using ReadStatus = void (*)(UObject*, uint8_t, void*);
        using FindCharacter = void* (*)(void*, uint8_t, uint8_t, int32_t);
        inline static const uintptr_t character_record_lookup_rva =
            character_native::rva(character_native::Site::character_record_lookup);
        static constexpr size_t active_save_data_offset = 0x870;
        static constexpr size_t pawn_base_parameters_offset = 0x4d0;
        static constexpr size_t base_parameters_record_offset = 0x130;
        static constexpr size_t camp_character_id_offset = 0xc9;
        static constexpr size_t menu_open_offset = 0xa1;
        inline static Impl* active{};
        // Borrowed only within the owning native tick; never retained across frames.
        inline static thread_local UObject* steam_roster_controller{};
        uintptr_t base{};
        bool steam{};
        size_t manager_save_owner_offset{0x140};
        size_t manager_catalog_offset{0x360};
        uintptr_t camp_vtable{0x7c2a488};
        uint64_t original{};
        uint64_t original_tick{};
        uint64_t original_record_lookup{};
        std::unique_ptr<PLH::x64Detour> hook;
        std::unique_ptr<PLH::x64Detour> tick_hook;
        std::unique_ptr<PLH::x64Detour> record_lookup_hook;
        void* record_lookup_stub{};
        FWeakObjectPtr portrait_camp_controller;
        FWeakObjectPtr portrait_hero, portrait_actor, portrait_source_mesh, portrait_source_asset;
        FWeakObjectPtr weapon_display_table;
        bool attempted{};

        static UObject* current_hero()
        {
            if (auto* hero = character_swap::current_hero())
            {
                return hero;
            }
            return Kh02Story::current_hero();
        }

        static uint8_t character_id(UObject* hero)
        {
            auto* property =
                is_uobject_valid_fast(hero) ? hero->GetPropertyByNameInChain(L"m_PlayerUniqueID") : nullptr;
            const auto player = property ? *property->ContainerPtrToValuePtr<uint8_t>(hero) : 255;
            switch (player)
            {
            case 8:
            case 9:
                return 2;
            case 10:
                return 6;
            case 16:
                return 3;
            case 17:
                return 7;
            default:
                return 0;
            }
        }

        struct Call : Kh02ReflectedCall
        {
            Call(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "Alternate camp portrait", 512)
            {
            }

            Call(const wchar_t* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "Alternate camp portrait", 512)
            {
            }
        };

        using Transform = std::array<std::byte, 48>;

        static auto component_transform(UObject* component) -> Transform
        {
            Call get(component, L"K2_GetComponentToWorld");
            get.run();
            auto* result = get.param(L"ReturnValue");
            if (!result || result->GetElementSize() != sizeof(Transform))
            {
                throw std::runtime_error("Alternate portrait transform layout");
            }
            Transform value{};
            std::memcpy(value.data(), result->ContainerPtrToValuePtr<void>(get.parameters.data()), value.size());
            return value;
        }

        static auto transform_parameter(const Transform& transform, std::array<float, 3> value, bool inverse,
                                        bool position) -> std::array<float, 3>
        {
            const auto* method = position  ? inverse ? L"InverseTransformLocation" : L"TransformLocation"
                                 : inverse ? L"InverseTransformDirection"
                                           : L"TransformDirection";
            Call convert(L"/Script/Engine.Default__KismetMathLibrary", method);
            auto* input = convert.param(L"T");
            if (!input || input->GetElementSize() != sizeof(Transform))
            {
                throw std::runtime_error("Alternate portrait material transform layout");
            }
            std::memcpy(input->ContainerPtrToValuePtr<void>(convert.parameters.data()), transform.data(),
                        transform.size());
            convert.vector(position ? L"Location" : L"Direction", value[0], value[1], value[2]);
            convert.run();
            return convert.result_vector();
        }

        static auto portrait_head_parameters(UObject* source_mesh, UObject* portrait_mesh)
            -> std::array<std::array<float, 4>, 4>
        {
            constexpr std::array names{L"atama_mtr_x", L"atama_mtr_y", L"atama_mtr_z", L"atama_WP"};
            const auto source_transform = component_transform(source_mesh);
            const auto portrait_transform = component_transform(portrait_mesh);
            Call material(source_mesh, L"GetMaterial");
            material.integer(L"ElementIndex", 0);
            material.run();
            std::array<std::array<float, 4>, 4> values{};
            for (size_t index = 0; index < names.size(); ++index)
            {
                Call parameter(material.result(), L"K2_GetVectorParameterValue");
                parameter.name(L"ParameterName", names[index]);
                parameter.run();
                auto* result = parameter.param(L"ReturnValue");
                if (!result || result->GetElementSize() != sizeof(values[index]))
                {
                    throw std::runtime_error("Alternate portrait head parameter layout");
                }
                std::memcpy(values[index].data(), result->ContainerPtrToValuePtr<void>(parameter.parameters.data()),
                            sizeof(values[index]));
                const bool position = index == 3;
                const auto local = transform_parameter(
                    source_transform, {values[index][0], values[index][1], values[index][2]}, true, position);
                const auto mapped = transform_parameter(portrait_transform, local, false, position);
                std::copy(mapped.begin(), mapped.end(), values[index].begin());
            }
            return values;
        }

        template <class T> static auto field(void* object, size_t offset) -> T
        {
            T value{};
            SIZE_T copied{};
            if (object)
            {
                ReadProcessMemory(GetCurrentProcess(), static_cast<std::byte*>(object) + offset, &value, sizeof(value),
                                  &copied);
            }
            return copied == sizeof(value) ? value : T{};
        }

        static void* find_character_record(void* save_data, uint8_t requested_character, void* native_record) noexcept
        {
            if (!active)
            {
                return native_record;
            }
            try
            {
                auto& self = *active;
                auto* hero = self.portrait_hero.Get();
                auto* camp = self.portrait_camp_controller.Get();
                if (hero && hero == current_hero() && is_uobject_valid_fast(camp) &&
                    character_id(hero) == requested_character &&
                    field<uint8_t>(camp, camp_character_id_offset) == requested_character)
                {
                    auto* menu = try_read_reflected_object_property_value(camp, L"m_CampMenu");
                    auto* manager = try_read_reflected_object_property_value(camp, L"m_UIManager");
                    auto* controller = try_read_reflected_object_property_value(manager, L"m_pPlayerController");
                    auto* save_owner = field<UObject*>(manager, self.manager_save_owner_offset);
                    auto* parameters = field<UObject*>(hero, pawn_base_parameters_offset);
                    auto* record = is_uobject_valid_fast(parameters)
                                       ? field<std::byte*>(parameters, base_parameters_record_offset)
                                       : nullptr;
                    if (is_uobject_valid_fast(menu) && field<uint8_t>(menu, menu_open_offset) &&
                        is_uobject_valid_fast(save_owner) &&
                        save_data == reinterpret_cast<std::byte*>(save_owner) + active_save_data_offset &&
                        try_read_reflected_object_property_value(controller, L"Pawn") == hero &&
                        try_read_reflected_object_property_value(hero, L"Controller") == controller &&
                        kh02_actor_world(hero) == kh02_current_world() && record &&
                        field<uint8_t>(record, 0) == requested_character)
                    {
                        // The camp equipment/ability readers and writers use the
                        // character-only lookup, which selects the party form for
                        // Roxas and Kairi. While their owned camp is open, resolve
                        // the same status record that their playable pawn consumes.
                        return record;
                    }
                }
            }
            catch (...)
            {
                // Do not unwind across the retail register-preserving adapter.
            }
            return native_record;
        }

        void prepare_weapon_display()
        {
            auto* table = find_loaded_runtime_asset(L"/Game/Load/Tres/TresItemData.TresItemData");
            if (!is_uobject_valid_fast(table) || weapon_display_table.Get() == table)
            {
                return;
            }
            auto* structure = field<UObject*>(table, 0x40);
            if (!is_uobject_valid_fast(structure) || structure->GetName() != L"TresItemDataTable")
            {
                return;
            }
            auto* row_struct = static_cast<UScriptStruct*>(structure);
            auto* key = find_struct_field_by_name(row_struct, L"m_KeyName");
            auto* help = find_struct_field_by_name(row_struct, L"m_HelpTextID");
            if (!key || key->GetClass().GetName() != L"NameProperty" || key->GetElementSize() != 8 || !help ||
                help->GetClass().GetName() != L"StrProperty" || help->GetElementSize() != 16)
            {
                return;
            }
            // Audited UE4.17 RowMap layout, behind this service's executable guard.
            const auto entries = field<std::byte*>(table, 0x48);
            const auto count = field<int32_t>(table, 0x50);
            const auto capacity = field<int32_t>(table, 0x54);
            if (!entries || count < 1 || count > 12000 || capacity < count || capacity > 20000)
            {
                return;
            }
            size_t updated = 0;
            for (int32_t index = 0; index < count; ++index)
            {
                auto* row = field<std::byte*>(entries + index * 24, 8);
                if (!row)
                {
                    continue;
                }
                const auto row_name = field<FName>(entries + index * 24, 0).ToString();
                for (const auto& display : alternate_weapon_displays)
                {
                    if (row_name != display.row || key->ContainerPtrToValuePtr<FName>(row)->ToString() != display.token)
                    {
                        continue;
                    }
                    if (!write_string_struct_field(help, row, display.help))
                    {
                        continue;
                    }
                    ++updated;
                    break;
                }
            }
            if (updated == alternate_weapon_displays.size())
            {
                weapon_display_table = make_uobject_weak(table);
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Alternate weapon menu metadata ready\n"));
            }
        }

        static void build_roster(UObject* camp_controller)
        {
            auto& self = *active;
            auto* hero = current_hero();
            if (is_uobject_valid_fast(hero))
            {
                self.prepare_weapon_display();
            }
            reinterpret_cast<BuildRoster>(self.original)(camp_controller);
            self.apply_roster(camp_controller);
        }

        static void reset_steam_roster(UObject* camp_controller)
        {
            auto& self = *active;
            reinterpret_cast<BuildRoster>(self.original)(camp_controller);
            if (camp_controller == steam_roster_controller)
            {
                // Steam inlines roster construction after this complete reset
                // function. Its primary-player mask excludes all four alternates,
                // leaving our row for the native name, asset and portrait loaders.
                self.apply_roster(camp_controller);
            }
        }

        void apply_roster(UObject* camp_controller)
        {
            auto& self = *this;
            auto* hero = current_hero();
            const auto unique_id = character_id(hero);
            if (!is_uobject_valid_fast(hero) || !unique_id || !is_uobject_valid_fast(camp_controller) ||
                field<uintptr_t>(camp_controller, 0) != self.base + self.camp_vtable ||
                kh02_actor_world(hero) != kh02_current_world())
            {
                return;
            }
            auto* manager = try_read_reflected_object_property_value(camp_controller, L"m_UIManager");
            if (!is_uobject_valid_fast(manager))
            {
                return;
            }
            auto* controller = try_read_reflected_object_property_value(manager, L"m_pPlayerController");
            if (!is_uobject_valid_fast(controller) ||
                try_read_reflected_object_property_value(controller, L"Pawn") != hero ||
                try_read_reflected_object_property_value(hero, L"Controller") != controller)
            {
                return;
            }
            auto* save_owner = field<UObject*>(manager, self.manager_save_owner_offset);
            auto* character_catalog = field<void*>(manager, self.manager_catalog_offset);
            // The status bank belongs to TresChrBaseParam, distinct from the
            // pawn's reflected TresEquipment component.
            auto* base_parameters = field<UObject*>(hero, 0x4d0);
            auto* record = is_uobject_valid_fast(base_parameters) ? field<std::byte*>(base_parameters, 0x130) : nullptr;
            if (!is_uobject_valid_fast(save_owner) || !character_catalog || !record ||
                field<uint8_t>(record, 0) != unique_id || field<uint8_t>(camp_controller, 0xc9) != 0)
            {
                return;
            }
            // Vanilla excludes alternate forms from its primary-player mask
            // and assumes Sora's character ID. Supply the actual character/form
            // pair so the status reader resolves the owned hero's equipment.
            auto* save_bank =
                self.steam ? reinterpret_cast<void* (*)(UObject*)>(
                                 self.base + character_native::rva(character_native::Site::camp_save_bank))(save_owner)
                           : reinterpret_cast<std::byte*>(save_owner) + active_save_data_offset;
            constexpr size_t story_progress_offset = 0x7df4;
            auto* character = reinterpret_cast<FindCharacter>(
                self.base + character_native::rva(character_native::Site::camp_character))(
                character_catalog, unique_id, field<uint8_t>(record, 1),
                field<int32_t>(save_bank, story_progress_offset));
            if (!character)
            {
                return;
            }
            for (size_t slot = 0; slot < 8; ++slot)
            {
                auto* flags = reinterpret_cast<uint8_t*>(record + 0x11c + slot * 8);
                *flags = kh02rewards::editable_item_slot_flags(*flags);
            }
            auto* roster = reinterpret_cast<std::byte*>(camp_controller) + 0xc8;
            roster[0] = static_cast<std::byte>(field<uint8_t>(record, 1));
            roster[1] = static_cast<std::byte>(unique_id);
            reinterpret_cast<ReadStatus>(self.base + character_native::rva(character_native::Site::camp_status))(
                save_owner, unique_id, roster + 4);
            if (const auto experience = Kh02Story::aqua_experience(hero))
            {
                // The native alternate status reader leaves total EXP at zero.
                // Level and thresholds already come from her real base parameters.
                std::memcpy(roster + 8, &*experience, sizeof(*experience));
            }
            // 0x80 holds the character-data row. The following two pointers
            // belong to the native portrait actor and its loaded status asset.
            std::memcpy(roster + 0x80, &character, sizeof(character));
            self.portrait_hero = make_uobject_weak(hero);
            self.portrait_camp_controller = make_uobject_weak(camp_controller);
            constexpr auto portrait_class_path = L"/Game/UI/Blueprints/CampTop/p_ex001_UIActor.p_ex001_UIActor_C";
            if (!is_uobject_valid_fast(find_loaded_runtime_asset_class(portrait_class_path)))
            {
                request_runtime_asset_class_load(hero, portrait_class_path, nullptr, 0x4b483350);
            }
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Alternate native camp inventory roster prepared\n"));
        }

        void prepare_portrait(UObject* actor, UObject* hero)
        {
            auto* source_mesh = try_read_reflected_object_property_value(hero, L"MyMesh");
            auto* mesh = try_read_reflected_object_property_value(actor, L"MeshComponent");
            auto* source_asset = is_uobject_valid_fast(source_mesh)
                                     ? try_read_reflected_object_property_value(source_mesh, L"SkeletalMesh")
                                     : nullptr;
            if (!is_uobject_valid_fast(mesh) || !is_uobject_valid_fast(source_asset))
            {
                return;
            }
            if (portrait_actor.Get() == actor && portrait_source_mesh.Get() == source_mesh &&
                portrait_source_asset.Get() == source_asset)
            {
                return;
            }
            Call set_mesh(mesh, L"SetSkeletalMesh");
            set_mesh.object(L"NewMesh", source_asset);
            set_mesh.boolean(L"bReinitPose", true);
            set_mesh.run();
            // The cloned materials below use the capture's head coordinates;
            // Sora's former animation must not replace those facial parameters.
            write_bool_function_param(mesh->GetPropertyByNameInChain(L"m_bDisableBonePoseToMaterialParam"), mesh, true);
            Call pose(mesh, L"SetMasterPoseComponent");
            pose.object(L"NewMasterBoneComponent", source_mesh);
            pose.run();
            Call link(actor, L"SetLinkMeshComponent");
            link.object(L"LinkMeshComponent", source_mesh);
            link.run();
            const auto head_parameters = portrait_head_parameters(source_mesh, mesh);
            constexpr std::array head_parameter_names{L"atama_mtr_x", L"atama_mtr_y", L"atama_mtr_z", L"atama_WP"};
            Call material_count(source_mesh, L"GetNumMaterials");
            material_count.run();
            const auto count =
                read_function_int_param(material_count.function, material_count.parameters.data(), L"ReturnValue")
                    .value_or(0);
            for (int index = 0; index < count && index < 64; ++index)
            {
                Call material(source_mesh, L"GetMaterial");
                material.integer(L"ElementIndex", index);
                material.run();
                auto* source_material = material.result();
                auto* parent = try_read_reflected_object_property_value(source_material, L"Parent");
                Call create(mesh, L"CreateDynamicMaterialInstance");
                create.integer(L"ElementIndex", index);
                create.object(L"SourceMaterial", is_uobject_valid_fast(parent) ? parent : source_material);
                create.run();
                auto* instance = create.result();
                Call copy(instance, L"K2_CopyMaterialInstanceParameters");
                copy.object(L"Source", source_material);
                copy.run();
                // Eye projection uses world-space head vectors. Reusing the
                // gameplay coordinates makes the relocated capture's eyes black.
                for (size_t parameter = 0; parameter < head_parameter_names.size(); ++parameter)
                {
                    const auto& value = head_parameters[parameter];
                    Call apply(instance, L"SetVectorParameterValue");
                    apply.name(L"ParameterName", head_parameter_names[parameter]);
                    apply.numbers(L"Value", {value[0], value[1], value[2], value[3]});
                    apply.run();
                }
            }
            portrait_actor = make_uobject_weak(actor);
            portrait_source_mesh = make_uobject_weak(source_mesh);
            portrait_source_asset = make_uobject_weak(source_asset);
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Alternate native camp portrait model prepared\n"));
        }

        static void tick_camp(UObject* camp_controller)
        {
            auto& self = *active;

            struct RosterScope
            {
                UObject* previous{steam_roster_controller};

                ~RosterScope()
                {
                    steam_roster_controller = previous;
                }
            } roster_scope;

            steam_roster_controller = nullptr;
            if (self.steam && is_uobject_valid_fast(camp_controller) && field<int32_t>(camp_controller, 0x58) == 2 &&
                (field<uint32_t>(camp_controller, 0x60) & 1) && is_uobject_valid_fast(current_hero()))
            {
                self.prepare_weapon_display();
                steam_roster_controller = camp_controller;
            }
            auto* hero = self.portrait_hero.Get();
            const bool alternate_camp = is_uobject_valid_fast(hero) && hero == current_hero() &&
                                        is_uobject_valid_fast(camp_controller) &&
                                        kh02_actor_world(hero) == kh02_current_world() &&
                                        field<uint8_t>(camp_controller, 0xc9) == character_id(hero);
            const bool spawning_portrait = alternate_camp && field<int32_t>(camp_controller, 0x58) == 7;
            auto* status = spawning_portrait ? field<UObject*>(camp_controller, 0x158) : nullptr;
            // Some alternate UICharacterData rows have a face icon but no status
            // asset path. Supply the real HUD/status asset after native loading,
            // immediately before the controller builds its portrait spawn queue.
            if (spawning_portrait && !status)
            {
                const auto player = field<uint8_t>(camp_controller, 0xc8);
                const auto* asset = player == 8    ? L"p_ex004"
                                    : player == 9  ? L"p_ex003"
                                    : player == 16 ? L"p_ex006"
                                    : player == 17 ? L"p_ex007"
                                                   : L"p_ex002";
                const auto path =
                    std::wstring{L"/Game/UI/Data/Character/"} + asset + L"_StatusData." + asset + L"_StatusData";
                status = find_loaded_runtime_asset(path.c_str());
                if (is_uobject_valid_fast(status))
                {
                    std::memcpy(reinterpret_cast<std::byte*>(camp_controller) + 0x158, &status, sizeof(status));
                }
            }
            auto* actor_class =
                spawning_portrait
                    ? find_loaded_runtime_asset_class(L"/Game/UI/Blueprints/CampTop/p_ex001_UIActor.p_ex001_UIActor_C")
                    : nullptr;
            auto* class_property =
                is_uobject_valid_fast(status) ? status->GetPropertyByNameInChain(L"CampTopUIActor") : nullptr;
            UObject** class_slot = class_property && class_property->GetElementSize() == sizeof(UObject*)
                                       ? class_property->ContainerPtrToValuePtr<UObject*>(status)
                                       : nullptr;
            // The native queue copies the class into its spawn request. Lend
            // the hero the normal capture actor only during this controller tick;
            // restore the shared status asset before returning to its owner.
            const bool lend_class = class_slot && !*class_slot && is_uobject_valid_fast(actor_class);
            if (lend_class)
            {
                *class_slot = actor_class;
            }
            reinterpret_cast<TickCamp>(self.original_tick)(camp_controller);
            if (lend_class)
            {
                *class_slot = nullptr;
            }
            if (alternate_camp)
            {
                auto* actor = field<UObject*>(camp_controller, 0x150);
                if (is_uobject_valid_fast(actor))
                {
                    try
                    {
                        self.prepare_portrait(actor, hero);
                    }
                    catch (const std::exception&)
                    {
                        Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Alternate camp portrait setup failed\n"));
                    }
                }
            }
        }

        void install()
        {
            if (attempted)
            {
                return;
            }
            attempted = true;
            base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            using character_native::Site;
            steam = character_native::build() == character_native::Build::Steam;
            if (!character_native::verify_sites(
                    {Site::camp_status, Site::camp_tick, Site::camp_character, Site::character_record_lookup}) ||
                !(steam ? character_native::verify_sites({Site::camp_roster_reset, Site::camp_save_bank})
                        : character_native::verify_sites({Site::camp_roster})))
            {
                return;
            }
            if (steam)
            {
                manager_save_owner_offset = 0x180;
                manager_catalog_offset = 0x3a0;
                camp_vtable = 0x6a43610;
            }
            active = this;
            const auto lookup_code =
                build_character_record_lookup_stub(reinterpret_cast<uint64_t>(&find_character_record),
                                                   reinterpret_cast<uint64_t>(&original_record_lookup));
            record_lookup_stub = VirtualAlloc(nullptr, lookup_code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (!record_lookup_stub)
            {
                active = nullptr;
                return;
            }
            std::memcpy(record_lookup_stub, lookup_code.data(), lookup_code.size());
            DWORD previous_protection{};
            if (!VirtualProtect(record_lookup_stub, lookup_code.size(), PAGE_EXECUTE_READ, &previous_protection))
            {
                VirtualFree(record_lookup_stub, 0, MEM_RELEASE);
                record_lookup_stub = nullptr;
                active = nullptr;
                return;
            }
            FlushInstructionCache(GetCurrentProcess(), record_lookup_stub, lookup_code.size());
            hook = std::make_unique<PLH::x64Detour>(
                base + character_native::rva(steam ? Site::camp_roster_reset : Site::camp_roster),
                steam ? reinterpret_cast<uintptr_t>(&reset_steam_roster) : reinterpret_cast<uintptr_t>(&build_roster),
                &original);
            tick_hook = std::make_unique<PLH::x64Detour>(base + character_native::rva(Site::camp_tick),
                                                         reinterpret_cast<uintptr_t>(&tick_camp), &original_tick);
            record_lookup_hook = std::make_unique<PLH::x64Detour>(base + character_record_lookup_rva,
                                                                  reinterpret_cast<uintptr_t>(record_lookup_stub),
                                                                  &original_record_lookup);
            record_lookup_hook->setDetourScheme(PLH::x64Detour::VALLOC2);
            if (!hook->hook() || !original || !tick_hook->hook() || !original_tick || !record_lookup_hook->hook() ||
                !original_record_lookup)
            {
                if (record_lookup_hook->isHooked())
                {
                    record_lookup_hook->unHook();
                }
                if (tick_hook->isHooked())
                {
                    tick_hook->unHook();
                }
                if (hook->isHooked())
                {
                    hook->unHook();
                }
                active = nullptr;
            }
        }

        ~Impl()
        {
            if (record_lookup_hook && record_lookup_hook->isHooked())
            {
                record_lookup_hook->unHook();
            }
            if (record_lookup_stub && (!record_lookup_hook || !record_lookup_hook->isHooked()))
            {
                VirtualFree(record_lookup_stub, 0, MEM_RELEASE);
            }
            if (tick_hook && tick_hook->isHooked())
            {
                tick_hook->unHook();
            }
            if (hook && hook->isHooked())
            {
                hook->unHook();
            }
            if (active == this)
            {
                active = nullptr;
            }
        }
    };

    Kh02InventoryMenu::Kh02InventoryMenu() : impl(std::make_unique<Impl>()) {}

    Kh02InventoryMenu::~Kh02InventoryMenu() = default;

    void Kh02InventoryMenu::install()
    {
        impl->install();
    }

    void Kh02InventoryMenu::install_shared()
    {
        static Kh02InventoryMenu shared;
        shared.install();
    }
} // namespace kh3ap
