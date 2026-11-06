#include "character_style.h"
#include "character_style_state.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <array>
#include <chrono>
#include <cstring>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json.hpp>
#endif
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_style
{
    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using Site = character_native::Site;
        constexpr int character_count = player_id_value(PlayerId::KingMickey) + 1;
        constexpr size_t style_level = 0xb34;
        constexpr size_t style_command = 0xb38;
        constexpr size_t style_remaining = 0xb54;
        constexpr size_t style_total = 0xb58;
        constexpr size_t style_finish_ready = 0xb5d;
        constexpr size_t animation_refresh_flags = 0x634;
        constexpr size_t equipment_weapon = 0x220;
        constexpr size_t equipment_slot = 0x378;
        constexpr size_t slot_bytes = 48;
        constexpr size_t slot_weapon_id = 8;
        constexpr size_t slot_form = 12;
        constexpr size_t slot_wear = 13;
        ReflectedPropertyCache<16> properties;
        std::array<std::optional<Form>, character_count> forms;
        FWeakObjectPtr world, last_hero;
        int target_id = -1;
        Clock::time_point deadline;

        template <typename T> T read(const void* owner, size_t offset)
        {
            T value{};
            std::memcpy(&value, static_cast<const std::byte*>(owner) + offset, sizeof(value));
            return value;
        }

        template <typename T> void write(void* owner, size_t offset, T value)
        {
            std::memcpy(static_cast<std::byte*>(owner) + offset, &value, sizeof(value));
        }

        int player_id(UObject* pawn)
        {
            auto* property = properties.resolve(pawn, L"m_PlayerUniqueID");
            return property && property->GetElementSize() == 1
                       ? *property->ContainerPtrToValuePtr<uint8_t>(pawn)
                       : -1;
        }

        bool supported()
        {
            return character_native::verify_sites(
                {Site::style_apply_form, Site::style_refresh_animation, Site::style_refresh_hud,
                 Site::style_offer_finish});
        }

        template <typename Function> Function native(Site site)
        {
            return reinterpret_cast<Function>(
                reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                character_native::rva(site));
        }

        struct Live
        {
            UObject* controller{};
            UObject* equipment{};
            std::byte* slot{};
            Form form;
        };

        std::optional<Live> sample(UObject* pawn)
        {
            if (!is_uobject_valid_fast(pawn) || !supported())
            {
                return {};
            }
            auto* controller = properties.read_object(pawn, L"Controller");
            auto* equipment = properties.read_object(pawn, L"MyEquipment");
            if (!is_uobject_valid_fast(controller) || !is_uobject_valid_fast(equipment) ||
                properties.read_object(controller, L"Pawn") != pawn ||
                !is_uobject_valid_fast(properties.read_object(controller, L"Player")))
            {
                return {};
            }
            auto* slots = properties.resolve(equipment, L"m_EquipSlots");
            auto* schema = slots && slots->IsA<FStructProperty>()
                               ? static_cast<FStructProperty*>(slots)->GetStruct()
                               : nullptr;
            if (!is_uobject_valid_fast(schema) || slots->GetArrayDim() != 3 ||
                slots->GetOffset_Internal() != 0x2e8 || slots->GetElementSize() != slot_bytes ||
                schema->GetName() != L"TresEquipWeaponSlotWork")
            {
                return {};
            }
            const int index = read<int>(equipment, equipment_slot);
            if (index < 0 || index >= 3)
            {
                return {};
            }
            auto* slot = slots->ContainerPtrToValuePtr<std::byte>(equipment, index);
            return Live{controller,
                        equipment,
                        slot,
                        {read<int>(controller, style_command), read<int>(controller, style_level),
                         read<int>(equipment, equipment_weapon), read<int>(slot, slot_weapon_id),
                         read<uint8_t>(slot, slot_form), read<uint8_t>(slot, slot_wear),
                         read<float>(controller, style_remaining),
                         read<float>(controller, style_total),
                         read<uint8_t>(controller, style_finish_ready) != 0}};
        }

        bool apply(UObject* pawn, Live& live, const Form& saved)
        {
            // Restore the controller before the native form setter: ChangeWeapon
            // banks the outgoing controller time into the equipment slot itself.
            write(live.controller, style_command, saved.command);
            write(live.controller, style_level, saved.level);
            write(live.controller, style_remaining, saved.remaining);
            write(live.controller, style_total, saved.total);
            // Spellweaver changes the controller's style, but keeps Aqua's
            // ordinary weapon/wear form. The native setter rejects that no-op.
            if ((saved.form != live.form.form || saved.wear != live.form.wear) &&
                !native<bool (*)(UObject*, uint8_t, uint8_t, bool, float)>(Site::style_apply_form)(
                    pawn, saved.form, saved.wear, false, 0.0f))
            {
                write(live.controller, style_command, live.form.command);
                write(live.controller, style_level, live.form.level);
                write(live.controller, style_remaining, live.form.remaining);
                write(live.controller, style_total, live.form.total);
                return false;
            }
            // Same animation refresh used by the shipping StyleChange state.
            native<void (*)(UObject*, bool)>(Site::style_refresh_animation)(pawn, false);
            write(pawn, animation_refresh_flags,
                  static_cast<uint8_t>(read<uint8_t>(pawn, animation_refresh_flags) | 1));
            write(live.controller, style_remaining, saved.remaining);
            write(live.controller, style_total, saved.total);
            // Publish the active style using its actual remaining/total values.
            native<void (*)(UObject*, int, float)>(Site::style_refresh_hud)(live.controller,
                                                                            saved.command, -1.0f);
            if (saved.finish_ready)
            {
                native<bool (*)(UObject*, bool, bool)>(Site::style_offer_finish)(live.controller,
                                                                                 false, true);
            }
            const auto after = sample(pawn);
            return after && after->form.command == saved.command &&
                   after->form.form == saved.form && after->form.wear == saved.wear &&
                   after->form.remaining == saved.remaining;
        }
    } // namespace

    void cancel()
    {
        target_id = -1;
    }

    void reset()
    {
        cancel();
        forms = {};
        world = {};
        last_hero = {};
    }

    bool pending()
    {
        if (target_id >= 0 && Clock::now() >= deadline)
        {
            cancel();
        }
        return target_id >= 0;
    }

    void capture(UObject* outgoing, int target)
    {
        cancel();
        if (!is_uobject_valid_fast(outgoing) || !supported())
        {
            return;
        }
        const int source = player_id(outgoing);
        if (source < 0 || source >= character_count || target < 0 || target >= character_count ||
            target == source)
        {
            return;
        }
        auto* current_world = kh02_actor_world(outgoing);
        if (!is_uobject_valid_fast(current_world) || current_world != kh02_current_world())
        {
            reset();
            return;
        }
        if (world.Get() != current_world || last_hero.Get() != outgoing)
        {
            // A map transition, Retry, or an unrelated factory replacement starts
            // a new chain. Never revive a form from the preceding life/world.
            forms = {};
        }
        world = make_uobject_weak(current_world);
        last_hero = make_uobject_weak(outgoing);
        const auto live = sample(outgoing);
        forms[source] = live && resumable(live->form) ? std::optional{live->form} : std::nullopt;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (forms[source])
        {
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] CharacterStyle captured player={} command={} remaining={}\n"),
                source, forms[source]->command, forms[source]->remaining);
        }
#endif
        target_id = target;
        deadline = Clock::now() + std::chrono::seconds(3);
    }

    void restore(UObject* incoming)
    {
        if (!pending() || !is_uobject_valid_fast(incoming))
        {
            return;
        }
        if (world.Get() != kh02_actor_world(incoming))
        {
            forms = {};
            cancel();
            return;
        }
        if (player_id(incoming) != target_id || !runtime_sora_control_ready(incoming))
        {
            return;
        }
        auto live = sample(incoming);
        if (!live)
        {
            return;
        }
        const auto saved = forms[target_id];
        if (saved && saved->weapon_id == live->form.weapon_id &&
            saved->weapon == live->form.weapon && apply(incoming, *live, *saved))
        {
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] CharacterStyle restored player={} command={} remaining={} "
                    "total={}\n"),
                target_id, saved->command, saved->remaining, saved->total);
        }
        last_hero = make_uobject_weak(incoming);
        cancel();
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    std::string inspect(UObject* pawn)
    {
        const auto live = sample(pawn);
        if (!live)
        {
            return "{\"available\":false}";
        }
        const auto& value = live->form;
        return nlohmann::json{{"available", true},
                              {"player", player_id(pawn)},
                              {"command", value.command},
                              {"level", value.level},
                              {"weapon", value.weapon},
                              {"weapon_id", value.weapon_id},
                              {"form", value.form},
                              {"wear", value.wear},
                              {"remaining", value.remaining},
                              {"total", value.total},
                              {"finish_ready", value.finish_ready},
                              {"pending", pending()}}
            .dump();
    }

    bool offer_for_test(UObject* pawn, int command)
    {
        const auto live = sample(pawn);
        if (!live || !runtime_sora_control_ready(pawn) ||
            !character_native::verify_sites({Site::style_offer_command}) ||
            !((player_id(pawn) == player_id_value(PlayerId::Aqua) && command == 153) ||
              (player_id(pawn) == player_id_value(PlayerId::Sora) &&
               ((live->form.weapon == 1 && command == 148) ||
                (live->form.weapon == 14 && command == 150)))))
        {
            return false;
        }
        native<void (*)(UObject*, int, float, UObject*)>(Site::style_offer_command)(
            live->controller, command, 30.0f, nullptr);
        return true;
    }
#endif
} // namespace kh3ap::character_style
