
#pragma once

#include "kh3ap/features/player/keyblades/keyblade_style_schedule.h"
#include <Unreal/FWeakObjectPtr.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Runtime-owned keyblade style state.  The GoA pak is deliberately not a
    // source of truth for style unlocks; this state is populated by AP grants
    // and re-applied to the live player controller after a weapon-change state
    // transition on the game thread.
    struct RuntimeKeybladeStyleState
    {
        std::unordered_set<std::wstring> unlocked_styles{};
        RC::Unreal::FWeakObjectPtr last_sora{};
        std::wstring last_weapon{};
        bool last_style{};
        bool last_special_mode{};
        bool ascension_mode{};
        bool special_anti_form_started{};
        RC::Unreal::FWeakObjectPtr ascension_actor{};
        bool ascension_actor_scan_requested{};
        bool ascension_scan_after_cinematic_exit{};
        std::chrono::steady_clock::time_point ascension_check_due{};
        bool ascension_checks_suspended{};
        bool force_apply{true};
        bool defer_apply_once{false};
        bool loaded_from_disk{false};
        bool keyblade_save_pending{false};
        RuntimeKeybladeStyleSchedule check_schedule{};
    };

    struct RuntimeKeybladeStyleSanityDeps
    {
        class RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir{};
        std::wstring_view bp_get_equip_weapon_id_path;
        std::wstring_view bp_set_ability_style_change_path;
        std::wstring_view bp_set_restrict_style_path;
        std::wstring_view bp_notify_start_special_anti_form_path;
        std::wstring_view tres_enemy_pawn_e_ex_301_class_path;
        std::wstring_view tres_enemy_pawn_e_ex_301_is_ascension_mode_path;
        std::wstring_view runtime_context_summary;
        std::chrono::steady_clock::time_point now{};
        bool debug_logging{false};
    };

    auto try_set_keyblade_style(RuntimeState& runtime_state, RC::Unreal::UObject* sora_pawn,
                                std::wstring_view runtime_token, bool show_info,
                                std::wstring* failure_details = nullptr) -> bool;

    // Mark the next game-thread sanity pass as required. The state-write hook
    // calls this when it observes TresPlayerState_WeaponChange; AP grants also
    // set force_apply so a newly received unlock is applied without a map load.
    auto mark_runtime_keyblade_weapon_changed(RuntimeState& runtime_state) -> void;

    auto mark_runtime_keyblade_menu_closed(RuntimeState& runtime_state) -> void;

    // Ask the next game-thread sanity pass to refresh the Xehanort actor cache.
    // The Sora state hook uses this for cinematic entry and exit edges.
    auto schedule_runtime_keyblade_ascension_check(RuntimeState& runtime_state,
                                                   bool entering_cinematic) -> void;

    auto process_runtime_keyblade_style_sanity(const RuntimeKeybladeStyleSanityDeps& deps) -> void;

    // The keyblade Sora holds, as its WEP_KEYBLADExx name, or nullopt when
    // it cannot be read. Game thread only.
    auto read_equipped_keyblade_name(RC::Unreal::UObject* sora) -> std::optional<std::wstring>;
} // namespace kh3ap
