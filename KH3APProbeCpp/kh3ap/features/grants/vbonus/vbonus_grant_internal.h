#pragma once

#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/features/grants/vbonus/vbonus_layout.h"
#include "kh3ap/features/grants/keyblades/keyblade_enhancement_profile.h"
#include "kh3ap/core/types.h"
#include "kh3ap/core/memory/checked_process_read.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <optional>
#include <string_view>

namespace kh3ap::vbonus_detail
{
    using RC::Unreal::UObject;

    // Image/function caches retain only immutable native addresses. Object and
    // save-bank pointers below are borrowed for the current validated grant.
    using GetSaveOwner = void* (*)();
    using AddVBonusToEquipment = bool (*)(void*, int32_t, uint8_t, uint8_t, bool);
    using AddVBonusToCharacterRecord = bool (*)(void*, uint8_t, uint8_t, bool);
    using AddVBonusToAbilityTable = bool (*)(void*, uint8_t, uint8_t, bool);
    using RefreshVBonusCharacterRecord = void (*)(void*, uint8_t);
    using NormalizeAbilityEntry = void (*)(void*);
    using ResolveVBonusHudOwner = UObject* (*)(UObject*);
    using PresentVBonusHud = void (*)(UObject*, UObject*, const ArrayHeader*);
    using ResolveVBonusStatDisplayAmounts = void (*)(int32_t, uint8_t, int32_t*, int32_t*);
    using ResolveEgsVBonusStatDisplayAmounts = void (*)(int32_t, int32_t*, int32_t*, int32_t);
    using ResolveMagicDisplayCommand = int32_t (*)(uint8_t, void*);
    using ResolveMagicShortcutManager = void* (*)();
    using RefreshMagicShortcuts = void (*)(void*, int32_t);

    struct NativeVBonusHudPresenter
    {
        UObject* controller{};
        UObject* hud_owner{};
        uint64_t hud_presenter{};
        PresentVBonusHud present{};
    };

    struct VanillaSoraAbilityBanks
    {
        bool persistent_only{};
        uint64_t module_base{};
        uint64_t save_owner{};
        uint64_t equipment{};
        uint64_t active_vbonus_target{};
        uint64_t active_record{};
        uint64_t staging_record{};
        int32_t difficulty{};
        AddVBonusToEquipment add_vbonus_to_equipment{};
        AddVBonusToCharacterRecord add_vbonus{};
        NormalizeAbilityEntry normalize_ability_entry{};
    };

    struct NativeVBonusDisplayHelpers
    {
        ResolveVBonusStatDisplayAmounts resolve_stat_amounts{};
        ResolveMagicDisplayCommand resolve_magic_command{};
        ResolveMagicShortcutManager resolve_magic_shortcut_manager{};
        RefreshMagicShortcuts refresh_magic_shortcuts{};
    };

    struct NativeVBonusModuleCache
    {
        HMODULE module{};
        const NativeKeybladeEnhancementProfile* profile{};
        uint64_t module_base{};
        bool ready{};
    };

    struct NativeVBonusBankFunctionCache
    {
        uint64_t module_base{};
        GetSaveOwner get_save_owner{};
        AddVBonusToEquipment add_vbonus_to_equipment{};
        AddVBonusToCharacterRecord add_vbonus{};
        NormalizeAbilityEntry normalize_ability_entry{};
        bool active_vbonus_target_is_sora_pawn{};
        std::wstring failure_reason{};
        bool ready{};
    };

    struct NativeVBonusHudFunctionCache
    {
        ResolveVBonusHudOwner resolve_hud_owner{};
        PresentVBonusHud present{};
        std::wstring failure_reason{};
        bool ready{};
    };

    struct NativeVBonusStatFunctionCache
    {
        ResolveVBonusStatDisplayAmounts resolve_stat_amounts{};
        std::wstring failure_reason{};
        bool ready{};
    };

    struct NativeVBonusMagicFunctionCache
    {
        ResolveMagicDisplayCommand resolve_magic_command{};
        ResolveMagicShortcutManager resolve_magic_shortcut_manager{};
        RefreshMagicShortcuts refresh_magic_shortcuts{};
        std::wstring failure_reason{};
        bool ready{};
    };

    template <typename T> auto try_read_process_value(uint64_t address) -> std::optional<T>
    {
        return try_read_current_process_value<T>(address);
    }

    auto try_read_process_bytes(uint64_t address, size_t size)
        -> std::optional<std::vector<std::byte>>;

    auto try_write_process_bytes(uint64_t address, const void* bytes, size_t size) -> bool;

    auto native_signature_matches(uint64_t address,
                                  const NativeKeybladeEnhancementSignature& expected) -> bool;

    auto native_vbonus_module_cache() -> const NativeVBonusModuleCache&;

    auto resolve_egs_vbonus_save_owner() -> void*;

    auto add_egs_vbonus_to_character_record(void* character_record, uint8_t bonus, uint8_t ability,
                                            bool mark_unseen) -> bool;

    auto resolve_egs_vbonus_stat_display_amounts(int32_t difficulty, uint8_t bonus,
                                                 int32_t* hp_amount, int32_t* mp_amount) -> void;

    auto resolve_egs_magic_shortcut_manager() -> void*;

    auto native_vbonus_bank_function_cache() -> const NativeVBonusBankFunctionCache&;

    auto native_vbonus_hud_function_cache() -> const NativeVBonusHudFunctionCache&;

    auto native_vbonus_stat_function_cache() -> const NativeVBonusStatFunctionCache&;

    auto native_vbonus_magic_function_cache() -> const NativeVBonusMagicFunctionCache&;

    auto try_resolve_native_vbonus_hud_presenter(RuntimeState& runtime_state,
                                                 NativeVBonusHudPresenter* presenter,
                                                 std::wstring* failure_reason,
                                                 UObject* display_hero = nullptr) -> bool;

    auto try_resolve_native_vbonus_display_helpers(uint64_t module_base, bool need_stat_amounts,
                                                   bool need_magic,
                                                   NativeVBonusDisplayHelpers* helpers,
                                                   std::wstring* failure_reason) -> bool;

    auto pack_native_vbonus_hud_entry(uint32_t type, uint32_t value) -> uint64_t;

    auto present_native_vbonus_hud_entries(const NativeVBonusHudPresenter& presenter,
                                           UObject* sora_pawn,
                                           const std::vector<uint64_t>& display_entries,
                                           std::wstring_view route_name) -> void;

    auto present_native_vbonus_hud_abilities(const NativeVBonusHudPresenter& presenter,
                                             UObject* sora_pawn,
                                             const std::vector<uint64_t>& ability_values,
                                             std::wstring_view route_name) -> void;

    auto fail_vanilla_ability_bank(std::wstring_view reason, std::wstring* failure_reason) -> bool;

    auto try_resolve_vanilla_sora_ability_banks(UObject* sora_pawn, VanillaSoraAbilityBanks* banks,
                                                std::wstring* failure_reason) -> bool;

    auto try_read_vanilla_ability_entry(uint64_t character_record, uint64_t ability_value)
        -> std::optional<uint32_t>;

    auto try_read_vanilla_vbonus_count(uint64_t character_record, uint64_t bonus_value)
        -> std::optional<uint8_t>;

    auto vbonus_source_count(uint32_t entry) -> uint32_t;

    auto durable_ability_source_count(uint32_t entry) -> size_t;

    auto add_durable_ability_sources(uint32_t entry, size_t copies) -> std::optional<uint32_t>;

    auto try_write_and_normalize_ability_entry(uint64_t entry_address, uint32_t value,
                                               NormalizeAbilityEntry normalizer)
        -> std::optional<uint32_t>;

    auto try_add_direct_durable_ability_source(const VanillaSoraAbilityBanks& banks,
                                               uint64_t ability_value, RuntimeState& runtime_state,
                                               NativeVBonusHudPresenter* presenter,
                                               bool& newly_granted, std::wstring* failure_reason,
                                               uint32_t auto_equip_mask = 0)
        -> bool;

    auto try_essential_auto_equip_mask(const VanillaSoraAbilityBanks& banks,
                                      UObject* sora_pawn, uint64_t ability_value,
                                      uint32_t* mask, std::wstring* failure_reason) -> bool;

    auto try_grant_unique_equipped_ability_source(const VanillaSoraAbilityBanks& banks,
                                                  uint64_t ability_value, bool* newly_granted,
                                                  RuntimeSoraAbilityEntryState* state,
                                                  std::wstring* failure_reason) -> bool;

    auto try_add_direct_durable_vbonus(const VanillaSoraAbilityBanks& banks, uint64_t bonus_value,
                                       std::wstring* failure_reason) -> bool;

    auto try_resolve_single_ability_value(const RuntimeItemGrantRequest& request,
                                          std::wstring* ability_token, uint64_t* ability_value,
                                          std::wstring* failure_reason) -> bool;

} // namespace kh3ap::vbonus_detail
