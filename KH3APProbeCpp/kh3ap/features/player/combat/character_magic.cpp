#include <optional>
#include "kh3ap/features/player/combat/magic_command_ids.h"
#include "kh3ap/features/player/combat/character_magic.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/characters/character_swap_stats.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/features/player/player_ids.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <cmath>
#include <cstring>
#include <stdexcept>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_magic
{
    using namespace RC::Unreal;
    namespace
    {
        enum MeterSlot : int
        {
            sora_meter,
            aqua_meter,
            kairi_meter,
            roxas_meter,
            riku_meter,
            mickey_meter,
            meter_count,
        };
        constexpr auto first_spell_meter = aqua_meter;
        constexpr auto last_spell_meter = roxas_meter;
        constexpr auto spell_meter_count = last_spell_meter - first_spell_meter + 1;
        // Additional shipping Sora variants share the same persistent MP meter.
        constexpr std::array sora_variant_player_ids{player_id_value(PlayerId::SoraHundredAcreWood),
                                                     player_id_value(PlayerId::SoraYozora)};
        constexpr auto magic_property_cache_capacity = 32;
        constexpr auto native_magic_unit_settings_bytes = 23;
        constexpr auto native_magic_unit_bytes = 40;
        constexpr auto max_magic_call_parameter_bytes = 64;
        constexpr auto restored_charge_transition_seconds = 1.0f;

        constexpr auto max_hud_parts = 256;
        constexpr auto hud_command_cache_offset = 24;
        constexpr auto hud_command_bytes_offset = 8;
        constexpr auto hud_magic_commands_offset = 0x48;
        constexpr auto hud_link_commands_offset = 0xd8;
        constexpr auto hud_command_group_bytes = 96;
        constexpr auto hud_command_slot_count = 9;
        constexpr auto hud_shortcuts_offset = 0xa8;
        constexpr auto hud_shortcuts_bytes = 48;
        constexpr auto hud_shortcut_group_count = 3;
        constexpr auto hud_shortcuts_per_group = 4;
        constexpr auto max_magic_damage_row_bytes = 4096;
        constexpr auto magic_charge_active_mask = 2;
        ReflectedPropertyCache<magic_property_cache_capacity> properties;
        constexpr size_t charge_flags_offset = 0x7c0;
        constexpr size_t charge_remaining_offset = 0x7c4;
        using SetCharge = void (*)(UObject*, bool, float);
        SetCharge set_charge{};
        void (*refresh_menu)(UObject*){};
        void (*refresh_shortcuts)(UObject*){};
        std::array<std::optional<Meter>, meter_count> meters;
        std::optional<magic_command::Commands> sora_commands;
        FWeakObjectPtr game_instance;

        int slot(UObject* pawn)
        {
            auto* field = properties.resolve(pawn, L"m_PlayerUniqueID");
            if (!field || field->GetElementSize() != sizeof(uint8_t))
            {
                return -1;
            }
            const int id = *field->ContainerPtrToValuePtr<uint8_t>(pawn);
            if (is_standard_sora_player_id(id) ||
                std::find(sora_variant_player_ids.begin(), sora_variant_player_ids.end(), id) !=
                    sora_variant_player_ids.end())
            {
                return sora_meter;
            }
            if (id == player_id_value(PlayerId::Aqua))
            {
                return aqua_meter;
            }
            if (id == player_id_value(PlayerId::Kairi))
            {
                return kairi_meter;
            }
            if (id == player_id_value(PlayerId::Roxas))
            {
                return roxas_meter;
            }
            if (id == player_id_value(PlayerId::Riku))
            {
                return riku_meter;
            }
            if (id == player_id_value(PlayerId::KingMickey))
            {
                return mickey_meter;
            }
            return -1;
        }

        int integer(UObject* pawn, const wchar_t* method)
        {
            return character_swap::character_integer_value(pawn, method);
        }

        void check_session()
        {
            auto* instance = properties.read_object(kh02_current_viewport(), L"GameInstance");
            if (instance != game_instance.Get())
            {
                meters = {};
                sora_commands.reset();
                game_instance = make_uobject_weak(instance);
            }
        }

        struct MagicUnit
        {
            uint8_t command;
            std::array<std::byte, native_magic_unit_settings_bytes> settings;
            UClass* asset0;
            UClass* asset1;
        };
        static_assert(sizeof(MagicUnit) == native_magic_unit_bytes);

        TArray<MagicUnit>* units(UObject* set)
        {
            auto* field = properties.resolve(set, L"m_Units");
            if (!field || !field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() !=
                    sizeof(MagicUnit))
            {
                throw std::runtime_error("character_magic_set_layout");
            }
            return field->ContainerPtrToValuePtr<TArray<MagicUnit>>(set);
        }

        struct SpellState
        {
            FWeakObjectPtr equipment, magic_set;
            magic_command::Commands commands{};
        };
        std::array<SpellState, spell_meter_count> spell_states;
        struct HudState
        {
            FWeakObjectPtr hud, data;
            magic_command::Commands commands{};
        };
        std::array<HudState, spell_meter_count> hud_states;

        struct HudCommandLayout
        {
            const wchar_t* property;
            int owner_offset;
            int struct_bytes;
            int cache_count;
            int command_stride;
        };
        constexpr HudCommandLayout magic_hud_layout{L"MagicCommands", hud_magic_commands_offset,
                                                    hud_command_group_bytes, hud_command_slot_count,
                                                    sizeof(uint8_t)};
        constexpr HudCommandLayout link_hud_layout{L"LinkCommands", hud_link_commands_offset,
                                                   hud_command_group_bytes, hud_command_slot_count,
                                                   sizeof(uint8_t)};
        constexpr HudCommandLayout shortcut_hud_layout{
            L"ShortcutCommands", hud_shortcuts_offset, hud_shortcuts_bytes,
            hud_shortcut_group_count, hud_shortcuts_per_group};

        constexpr bool valid_hud_layout(const HudCommandLayout& layout)
        {
            return layout.cache_count > 0 && layout.command_stride > 0 &&
                   hud_command_bytes_offset + layout.cache_count * layout.command_stride <=
                       hud_command_cache_offset &&
                   hud_command_cache_offset + layout.cache_count * sizeof(void*) <=
                       layout.struct_bytes;
        }
        static_assert(valid_hud_layout(magic_hud_layout));
        static_assert(valid_hud_layout(link_hud_layout));
        static_assert(valid_hud_layout(shortcut_hud_layout));

        std::span<uint8_t> copy_hud_commands(UObject* source, UObject* copy,
                                             const HudCommandLayout& layout)
        {
            auto* field = properties.resolve(source, layout.property);
            if (!field || !field->IsA<FStructProperty>() ||
                field->GetOffset_Internal() != layout.owner_offset ||
                static_cast<FStructProperty*>(field)->GetStruct()->GetStructureSize() !=
                    layout.struct_bytes)
            {
                throw std::runtime_error("character_magic_hud_layout");
            }
            auto* original = field->ContainerPtrToValuePtr<std::byte>(source);
            auto* destination = field->ContainerPtrToValuePtr<std::byte>(copy);
            // Authored cache pointers must reference their own command bytes.
            // Rebind every pointer after copying the native struct.
            for (int index = 0; index < layout.cache_count; ++index)
            {
                uint8_t* cached{};
                std::memcpy(&cached, original + hud_command_cache_offset + index * sizeof(void*),
                            sizeof(cached));
                if (cached != reinterpret_cast<uint8_t*>(original + hud_command_bytes_offset +
                                                         index * layout.command_stride))
                {
                    throw std::runtime_error("character_magic_hud_cache_layout");
                }
            }
            static_cast<FStructProperty*>(field)->GetStruct()->CopyScriptStruct(destination,
                                                                                original);
            for (int index = 0; index < layout.cache_count; ++index)
            {
                auto* cached =
                    destination + hud_command_bytes_offset + index * layout.command_stride;
                std::memcpy(destination + hud_command_cache_offset + index * sizeof(void*), &cached,
                            sizeof(cached));
            }
            return {reinterpret_cast<uint8_t*>(destination + hud_command_bytes_offset),
                    static_cast<size_t>(layout.cache_count * layout.command_stride)};
        }

        bool prepare_hud(UObject* pawn, int index, const magic_command::Commands& commands)
        {
            auto* manager = properties.read_object(
                properties.read_object(kh02_current_viewport(), L"GameInstance"), L"m_UIManager");
            if (properties.read_object(manager, L"m_pPlayerController") !=
                properties.read_object(pawn, L"Controller"))
            {
                return false;
            }
            auto* list = properties.resolve(manager, L"m_UIPartsAry");
            if (!list || !list->IsA<FArrayProperty>())
            {
                return false;
            }
            auto* inner = static_cast<FArrayProperty*>(list)->GetInner();
            if (!inner || inner->GetElementSize() != sizeof(UObject*) ||
                inner->GetClass().GetName() != L"ObjectProperty")
            {
                return false;
            }
            const auto& parts = *list->ContainerPtrToValuePtr<TArray<UObject*>>(manager);
            if (parts.Num() < 0 || parts.Num() > max_hud_parts)
            {
                return false;
            }
            UObject* hud{};
            for (auto* part : parts)
            {
                if (is_uobject_valid_fast(part) &&
                    part->GetClassPrivate()->GetName() == L"TresUIP_HudCommand" &&
                    properties.read_object(part, L"m_pUIManager") == manager)
                {
                    hud = part;
                    break;
                }
            }
            auto* source = properties.read_object(hud, L"m_HudCommandDataAsset");
            if (!source)
            {
                return false;
            }
            auto& state = hud_states[index - first_spell_meter];
            if (state.hud.Get() == hud && state.data.Get() == source && state.commands == commands)
            {
                return true;
            }
            FStaticConstructObjectParameters parameters(source->GetClassPrivate(), hud);
            parameters.SetFlags = RF_Transient;
            auto* copy = UObjectGlobals::StaticConstructObject(parameters);
            if (!is_uobject_valid_fast(copy))
            {
                throw std::runtime_error("character_magic_hud_create");
            }
            auto magic = copy_hud_commands(source, copy, magic_hud_layout);
            for (size_t index = 0; index < magic.size(); ++index)
            {
                magic[index] = index < commands.size() ? commands[index] : 0;
            }
            (void)copy_hud_commands(source, copy, link_hud_layout);
            auto shortcuts = copy_hud_commands(source, copy, shortcut_hud_layout);
            for (auto& command : shortcuts)
            {
                if (const auto family = magic_command::normal_family(command))
                {
                    command = commands[*family];
                }
            }
            auto* field = properties.resolve_object(hud, L"m_HudCommandDataAsset");
            if (!field)
            {
                throw std::runtime_error("character_magic_hud_asset_property");
            }
            *field->ContainerPtrToValuePtr<UObject*>(hud) = copy;
            state = {make_uobject_weak(hud), make_uobject_weak(copy), commands};
            refresh_menu(hud);
            refresh_shortcuts(hud);
            return true;
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
            Kh02DataTableView sora(source, schema, "character_magic_source_damage_layout");
            Kh02DataTableView alternate(destination, schema,
                                        "character_magic_destination_damage_layout");
            const auto bytes = sora.schema->GetStructureSize();
            if (sora.schema != alternate.schema || bytes < 1 || bytes > max_magic_damage_row_bytes)
            {
                throw std::runtime_error("character_magic_damage_schema");
            }
            struct CopyContext
            {
                Kh02DataTableView& destination;
                UScriptStruct* schema;
                int bytes;
            };
            CopyContext context{alternate, sora.schema, bytes};
            if (!sora.visit(
                    [](unsigned long long key, void* row, void* opaque)
                    {
                        auto& context = *static_cast<CopyContext*>(opaque);
                        FName name;
                        std::memcpy(&name, &key, sizeof(name));
                        if (!name.ToString().starts_with(L"MAG_") || context.destination.find(key))
                        {
                            return;
                        }
                        struct RowDeleter
                        {
                            UScriptStruct* schema;
                            bool initialized{};
                            void operator()(void* row) const noexcept
                            {
                                if (initialized)
                                {
                                    schema->DestroyStruct(row);
                                }
                                FMemory::Free(row);
                            }
                        };
                        std::unique_ptr<void, RowDeleter> copy{FMemory::Malloc(context.bytes),
                                                               RowDeleter{context.schema}};
                        if (!copy)
                        {
                            throw std::bad_alloc();
                        }
                        context.schema->InitializeStruct(copy.get());
                        copy.get_deleter().initialized = true;
                        context.schema->CopyScriptStruct(copy.get(), row);
                        if (!context.destination.insert(key, copy.get()))
                        {
                            throw std::runtime_error("character_magic_damage_insert");
                        }
                        (void)copy.release();
                    },
                    &context))
            {
                throw std::runtime_error("character_magic_damage_visit");
            }
        }
    } // namespace

    bool initialize()
    {
        if (set_charge)
        {
            return true;
        }
        using character_native::Site;
        if (!character_native::verify_sites({Site::mp_charge_set, Site::mp_charge_flag,
                                             Site::mp_charge_remaining, Site::magic_menu_refresh,
                                             Site::magic_shortcut_refresh}))
        {
            return false;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        set_charge = reinterpret_cast<SetCharge>(base + character_native::rva(Site::mp_charge_set));
        refresh_menu = reinterpret_cast<void (*)(UObject*)>(
            base + character_native::rva(Site::magic_menu_refresh));
        refresh_shortcuts = reinterpret_cast<void (*)(UObject*)>(
            base + character_native::rva(Site::magic_shortcut_refresh));
        return true;
    }

    std::optional<Meter> read_meter(UObject* pawn)
    {
        if (!set_charge || slot(pawn) < 0)
        {
            return std::nullopt;
        }
        const auto* data = reinterpret_cast<const std::byte*>(pawn);
        const bool charging =
            (std::to_integer<uint8_t>(data[charge_flags_offset]) & magic_charge_active_mask) != 0;
        float remaining{};
        if (charging)
        {
            std::memcpy(&remaining, data + charge_remaining_offset, sizeof(remaining));
        }
        if (!std::isfinite(remaining) || remaining < 0.0f)
        {
            return std::nullopt;
        }
        return Meter{integer(pawn, L"GetMagicPoint"), charging, remaining};
    }

    bool manages_meter(UObject* pawn)
    {
        const auto index = slot(pawn);
        return index >= 0 && !kh02_current_story_world() &&
               (meters[index].has_value() || character_swap::owns_swap_pawn(pawn));
    }

    void write_meter(UObject* pawn, const Meter& meter)
    {
        if (!set_charge || slot(pawn) < 0)
        {
            return;
        }
        // SetMagicPoint ignores writes during recharge. Use the native charge
        // transition first, preserving the other bits in the same flag byte.
        set_charge(pawn, false, 0.0f);
        Kh02ReflectedCall call(pawn, L"SetMagicPoint", "character_magic",
                               max_magic_call_parameter_bytes);
        call.integer(L"InMP", std::clamp(meter.mp, 0, integer(pawn, L"GetMaxMagicPoint")));
        call.run();
        if (meter.charging)
        {
            set_charge(pawn, true, restored_charge_transition_seconds);
            // Zero remaining completes through the normal next MP update;
            // it must not become an empty, non-recharging meter on return.
            std::memcpy(reinterpret_cast<std::byte*>(pawn) + charge_remaining_offset,
                        &meter.remaining, sizeof(meter.remaining));
        }
    }

    void capture(UObject* pawn)
    {
        check_session();
        const auto index = slot(pawn);
        const auto meter = read_meter(pawn);
        if (index < 0 || !meter)
        {
            throw std::runtime_error("character_magic_meter_capture");
        }
        if (index == sora_meter)
        {
            magic_command::Commands commands{};
            if (!try_read_runtime_sora_magic_commands(pawn, commands))
            {
                throw std::runtime_error("character_magic_commands_capture");
            }
            sora_commands = commands;
        }
        meters[index] = meter;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] CharacterMagic capture slot={} mp={} charge={} remaining={}\n"),
            index, meter->mp, meter->charging, meter->remaining);
#endif
    }

    void restore(UObject* pawn)
    {
        check_session();
        const auto index = slot(pawn);
        if (index < 0)
        {
            throw std::runtime_error("character_magic_meter_restore");
        }
        const auto meter =
            meters[index].value_or(Meter{integer(pawn, L"GetMaxMagicPoint"), false, 0.0f});
        write_meter(pawn, meter);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] CharacterMagic restore slot={} mp={} charge={} remaining={}\n"),
            index, meter.mp, meter.charging, meter.remaining);
#endif
    }

    bool refresh_spells(UObject* pawn)
    {
        const auto index = slot(pawn);
        if (index < first_spell_meter || index > last_spell_meter ||
            !character_swap::owns_swap_pawn(pawn))
        {
            return true;
        }
        // Capture while Sora still owns his equipment/bank. The incoming
        // alternate is intentionally not a reward context until preparation
        // finishes; querying that grant context here would deadlock readiness.
        if (!sora_commands)
        {
            return false;
        }
        const auto& commands = *sora_commands;
        auto* equipment = properties.read_object(pawn, L"MyEquipment");
        auto* current = properties.read_object(equipment, L"m_MagicSet");
        auto& state = spell_states[index - first_spell_meter];
        if (state.equipment.Get() == equipment && state.magic_set.Get() == current &&
            state.commands == commands)
        {
            return prepare_hud(pawn, index, commands);
        }
        auto* source = find_loaded_runtime_asset(sora_magic_set_path);
        if (!source)
        {
            return false;
        }
        prepare_damage(pawn);
        FStaticConstructObjectParameters parameters(source->GetClassPrivate(), equipment);
        parameters.SetFlags = RF_Transient;
        auto* private_set = UObjectGlobals::StaticConstructObject(parameters);
        if (!is_uobject_valid_fast(private_set))
        {
            throw std::runtime_error("character_magic_set_create");
        }
        auto* destination = units(private_set);
        destination->Empty();
        for (const auto& unit : *units(source))
        {
            if (magic_command::retains_implementation(unit.command, commands))
            {
                destination->Add(unit);
            }
        }
        auto* field = properties.resolve_object(equipment, L"m_MagicSet");
        if (!field)
        {
            throw std::runtime_error("character_magic_equipment_layout");
        }
        *field->ContainerPtrToValuePtr<UObject*>(equipment) = private_set;
        state = {make_uobject_weak(equipment), make_uobject_weak(private_set), commands};
        return prepare_hud(pawn, index, commands);
    }

    bool owns_command(UObject* controller, int command)
    {
        if (!sora_commands || !magic_command::is_selected_command(command, *sora_commands))
        {
            return false;
        }
        auto* pawn = properties.read_object(controller, L"Pawn");
        const auto index = slot(pawn);
        return index >= first_spell_meter && index <= last_spell_meter &&
               character_swap::owns_swap_pawn(pawn) &&
               properties.read_object(pawn, L"Controller") == controller &&
               spell_states[index - first_spell_meter].equipment.Get() ==
                   properties.read_object(pawn, L"MyEquipment");
    }
} // namespace kh3ap::character_magic
