#include "keyblade_options_helpers.h"
#include "keyblade_style_catalog.h"
#include "keyblade_style_persistence.h"
#include "keyblade_weapon_lookup.h"
#include "keyblade_style_controller.h"
#include "keyblade_ascension_sync.h"
#include "kh3ap/features/player/combat/aqua_style_unlock.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <format>
#include <vector>

namespace kh3ap
{
    namespace
    {

        using namespace keyblade_options;
        using namespace RC;
        using namespace RC::Unreal;

        constexpr uint8_t s_style_id = 114;

    } // namespace

    auto try_set_keyblade_style(RuntimeState& runtime_state, UObject* sora,
                                std::wstring_view runtime_token, bool show_info,
                                std::wstring* failure_details) -> bool
    {
        if (failure_details)
        {
            failure_details->clear();
        }
        if (!runtime_token.starts_with(s_runtime_token_prefix))
        {
            return fail(failure_details,
                        std::format(STR("unsupported keyblade style token={}"), runtime_token));
        }

        const std::wstring_view keyblade_name = runtime_token.substr(s_runtime_token_prefix.size());
        if (!is_valid_keyblade_name(keyblade_name))
        {
            return fail(failure_details,
                        std::format(STR("invalid keyblade style name={}"), keyblade_name));
        }

        const auto ability_value = keyblade_style_ability_value(keyblade_name);
        RuntimeSoraAbilityEntryState ability_state{};
        bool newly_granted = false;
        std::wstring ability_failure{};
        if (!ability_value.has_value() ||
            !try_grant_unique_equipped_runtime_sora_ability(*ability_value, sora, &newly_granted,
                                                            &ability_state, &ability_failure))
        {
            return fail(failure_details, ability_failure.empty()
                                             ? STR("keyblade style ability grant failed")
                                             : std::move(ability_failure));
        }

        if (newly_granted && show_info)
        {
            const std::wstring ability_token =
                std::format(STR("ETresAbilityKind::AP_KEYBLADE_STYLE_{}"),
                            keyblade_name.substr(s_keyblade_name_prefix.size()));
            std::wstring popup_failure{};
            if (!try_present_native_vbonus_hud_ability_tokens(
                    std::vector<std::wstring>{ability_token}, sora, runtime_state, &popup_failure))
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Keyblade style ability HUD failed keyblade={} "
                        "ability_token={} error={}\n"),
                    keyblade_name, ability_token,
                    popup_failure.empty() ? STR("unknown") : popup_failure);
            }
        }

        auto& state = runtime_state.keyblade_style_state();
        const bool inserted = state.unlocked_styles.insert(std::wstring{keyblade_name}).second;
        state.force_apply = true;
        state.keyblade_save_pending = state.keyblade_save_pending || inserted;

        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime keyblade style recorded name={} inserted={} total={}\n"),
            keyblade_name, inserted ? STR("true") : STR("false"), state.unlocked_styles.size());
        return true;
    }

    auto mark_runtime_keyblade_weapon_changed(RuntimeState& runtime_state) -> void
    {
        runtime_state.keyblade_style_state().force_apply = true;
    }

    auto mark_runtime_keyblade_menu_closed(RuntimeState& runtime_state) -> void
    {
        auto& state = runtime_state.keyblade_style_state();
        state.force_apply = true;
        state.defer_apply_once = true;
    }

    auto process_runtime_keyblade_style_sanity(const RuntimeKeybladeStyleSanityDeps& deps) -> void
    {
        auto& state = deps.runtime_state.keyblade_style_state();
        load_persisted_styles(state, deps.bridge_dir);

        auto& pawn_state = deps.runtime_state.grant_state().pawn_state();
        UObject* const sora = pawn_state.object();
        UObject* const controller = pawn_state.controller_object();
        if (!prepare_keyblade_style_context(state, sora, controller, pawn_state.candidate_trusted(),
                                            deps))
        {
            return;
        }

        // Weapon changes, AP grants, controller refresh and menu close already mark
        // force_apply. Pawn settling and cinematic/ascension checks above must run
        // before this gate. Keep a slow recovery pass for missed ability changes;
        // dirty/deferred work and failed-read retries retain the existing cadence.
        if (!state.check_schedule.take(deps.now, state.force_apply || state.defer_apply_once ||
                                                     state.keyblade_save_pending))
        {
            return;
        }

        std::wstring weapon_failure{};
        std::wstring weapon_diagnostics{};
        const auto raw_weapon_id = try_read_equipped_weapon_id(
            sora, deps.bp_get_equip_weapon_id_path, &weapon_failure, &weapon_diagnostics);
        if (!raw_weapon_id.has_value())
        {
            state.force_apply = true;
            if (deps.debug_logging)
            {
                Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Keyblade style sanity waiting "
                                                    "for equipped weapon reason={}\n"),
                                                weapon_failure);
            }
            return;
        }

        auto weapon_name =
            resolve_keyblade_name_via_blueprint_enum_utility(*raw_weapon_id, &weapon_diagnostics);
        if (!weapon_name.has_value())
        {
            weapon_name = resolve_keyblade_name_fallback(*raw_weapon_id);
        }
        if (!weapon_name.has_value())
        {
            // Aqua owns Spellweaver independently of Sora's per-keyblade styles.
            // Other unmapped story weapons keep their normal style behavior.
            const bool style_enabled = sora->GetClassPrivate()->GetName() != L"p_ex002_Pawn_C" ||
                                       aqua_style_unlock::owned();
            std::wstring call_failure{};
            const bool style_applied =
                try_call_controller_bool(controller, deps.bp_set_ability_style_change_path,
                                         STR("InEnable"), style_enabled, &call_failure);
            const bool restrict_applied = try_call_controller_bool(
                controller, deps.bp_set_restrict_style_path, STR("Restrict"), false, &call_failure);
            if (!style_applied || !restrict_applied)
            {
                state.force_apply = true;
                if (deps.debug_logging)
                {
                    Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Keyblade style sanity waiting for unmapped weapon "
                            "style apply error={}\n"),
                        call_failure);
                }
                return;
            }
            state.last_sora = make_uobject_weak(sora);
            state.last_weapon.clear();
            state.last_style = style_enabled;
            state.last_special_mode = false;
            state.force_apply = false;
            save_persisted_styles(state, deps.bridge_dir);
            return;
        }

        const auto style_ability_value = keyblade_style_ability_value(*weapon_name);
        RuntimeSoraAbilityEntryState style_ability_state{};
        std::wstring style_ability_failure{};
        if (!style_ability_value.has_value() ||
            !try_read_runtime_sora_ability_entry_state(
                *style_ability_value, sora, &style_ability_state, &style_ability_failure))
        {
            state.force_apply = true;
            if (deps.debug_logging)
            {
                Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Keyblade style sanity waiting "
                                                    "for style ability reason={}\n"),
                                                style_ability_failure);
            }
            return;
        }

        // Runtime ability entries are rebuilt when Sora's pawn is recreated. If
        // the bridge says this style was already received, restore the missing
        // owned/equipped entry. An owned but unequipped entry is left alone so a
        // manual ability-menu toggle still takes effect.
        if (!style_ability_state.owned && state.unlocked_styles.contains(*weapon_name))
        {
            bool newly_granted = false;
            if (!try_grant_unique_equipped_runtime_sora_ability(
                    *style_ability_value, sora, &newly_granted, &style_ability_state,
                    &style_ability_failure))
            {
                state.force_apply = true;
                if (deps.debug_logging)
                {
                    Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Keyblade style ability "
                                                        "regrant waiting weapon={} reason={}\n"),
                                                    *weapon_name, style_ability_failure);
                }
                return;
            }
        }

        if (state.defer_apply_once)
        {
            state.defer_apply_once = false;
            return;
        }

        // Keep style change enabled after Anti Form starts during this encounter.
        RuntimeSoraAbilityEntryState global{};
        std::wstring style_fail{};
        const bool global_style =
            try_read_runtime_sora_ability_entry_state(s_style_id, sora, &global, &style_fail) &&
            global.owned && global.equipped;
        const bool special_mode = state.ascension_mode || state.special_anti_form_started;
        const bool keyblade_has_style_change =
            global_style || (style_ability_state.owned && style_ability_state.equipped);
        const bool style_enabled = special_mode || keyblade_has_style_change;
        const bool state_changed = state.force_apply || state.last_sora.Get() != sora ||
                                   state.last_weapon != *weapon_name ||
                                   state.last_style != style_enabled ||
                                   state.last_special_mode != special_mode;

        if (state_changed)
        {
            std::wstring call_failure{};
            const bool style_applied =
                try_call_controller_bool(controller, deps.bp_set_ability_style_change_path,
                                         STR("InEnable"), style_enabled, &call_failure);
            const bool restrict_applied =
                try_call_controller_bool(controller, deps.bp_set_restrict_style_path,
                                         STR("Restrict"), !style_enabled, &call_failure);

            if (special_mode && !keyblade_has_style_change && !state.special_anti_form_started &&
                style_applied && restrict_applied &&
                !deps.bp_notify_start_special_anti_form_path.empty())
            {
                state.special_anti_form_started = try_call_controller_no_params(
                    controller, deps.bp_notify_start_special_anti_form_path, &call_failure);
            }

            if (!style_applied || !restrict_applied)
            {
                // Do not spin the game-thread update loop when a cooked build has
                // a slightly different reflected signature.  The next actual
                // weapon-change edge or AP style grant will set force_apply again
                // and give us another opportunity to resolve the function.
                state.force_apply = false;
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Keyblade style apply failed weapon={} enabled={} "
                        "special={} error={}\n"),
                    *weapon_name, style_enabled ? STR("true") : STR("false"),
                    special_mode ? STR("true") : STR("false"), call_failure);
                return;
            }

            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Keyblade style applied weapon={} raw_weapon_id={} "
                    "raw_hex=0x{:X} enabled={} special={} {}\n"),
                *weapon_name, *raw_weapon_id, *raw_weapon_id,
                style_enabled ? STR("true") : STR("false"),
                special_mode ? STR("true") : STR("false"), weapon_diagnostics);
        }

        state.last_sora = make_uobject_weak(sora);
        state.last_weapon = *weapon_name;
        state.last_style = style_enabled;
        state.last_special_mode = special_mode;
        state.force_apply = false;
        save_persisted_styles(state, deps.bridge_dir);
    }

} // namespace kh3ap
