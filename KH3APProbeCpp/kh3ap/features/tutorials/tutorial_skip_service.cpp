#include "kh3ap/features/tutorials/tutorial_patch_helpers.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/core/objects/asset_load_helpers.h"

#include "kh3ap/features/story/world_fixes/final_world_guidance_bypass.h"
#include "kh3ap/features/tutorials/olympus_tutorial_bypass.h"
#include "kh3ap/features/tutorials/world_keyblade_bypass.h"
#include "kh3ap/features/tutorials/tutorial_popup_bypass.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <cstdint>
#include <exception>
#include <string>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        using tutorial_patch::apply_tutorial_patch_batch;
        using tutorial_patch::combine_tutorial_patch_results;
        using tutorial_patch::disable_cactuar_camera_directors;
        using tutorial_patch::FinalWorldPatchAttempt;
        using tutorial_patch::find_constructed_or_live_function;
        using tutorial_patch::find_live_function;
        using tutorial_patch::guidance_result_name;
        using tutorial_patch::is_live_function_patch_target;
        using tutorial_patch::olympus_tutorial_result_name;
        using tutorial_patch::try_patch_final_world_function;
        using tutorial_patch::try_patch_tutorial_function_object;
        using tutorial_patch::tutorial_function_script;
        using tutorial_patch::tutorial_popup_result_name;
        using tutorial_patch::TutorialPatchAttempt;
        using tutorial_patch::TutorialPatcher;

        constexpr std::array<TutorialPatcher, 4> s_olympus_completion_patchers{
            &try_patch_runtime_olympus_shotlock_tutorial_bytecode,
            &try_patch_runtime_olympus_he01_team_attacks_help_bytecode,
            &try_patch_runtime_olympus_airstepping_tutorial_bytecode,
            &try_patch_runtime_olympus_he01_tutorial_ability_sets_bytecode,
        };

        constexpr std::array<TutorialPatcher, 5> s_olympus_early_patchers{
            &try_patch_runtime_olympus_casting_magic_tutorial_bytecode,
            &try_patch_runtime_olympus_free_running_tutorial_bytecode,
            &try_patch_runtime_olympus_flowmotion_help_bytecode,
            &try_patch_runtime_olympus_attractions_help_bytecode,
            &try_patch_runtime_olympus_he02_tutorial_ability_sets_bytecode,
        };

        constexpr std::array<TutorialPatcher, 8> s_olympus_save_points_patchers{
            &try_patch_runtime_olympus_flowmotion_tutorial_bytecode,
            &try_patch_runtime_olympus_team_attacks_help_bytecode,
            &try_patch_runtime_olympus_grand_magic_help_bytecode,
            &try_patch_runtime_olympus_formchange_tutorial_bytecode,
            &try_patch_runtime_olympus_attractions_tutorial_bytecode,
            &try_patch_runtime_olympus_he03_tutorial_ability_sets_bytecode,
            &try_patch_runtime_olympus_save_points_tutorial_bytecode,
            &try_patch_runtime_olympus_moogle_shop_tutorial_bytecode,
        };

        constexpr std::array<TutorialPatcher, 4> s_twilight_town_patchers{
            &try_patch_runtime_twilight_town_taking_photos_help_bytecode,
            &try_patch_runtime_twilight_town_lucky_emblems_help_bytecode,
            &try_patch_runtime_twilight_town_links_help_bytecode,
            &try_patch_runtime_twilight_town_meow_wow_help_bytecode,
        };

        constexpr std::array<TutorialPatcher, runtime_toy_box_tutorial_variant_count>
            s_toy_box_tutorial_patchers{
                &try_patch_runtime_toy_box_aerial_recovery_help_bytecode,
                &try_patch_runtime_toy_box_gigas_help_bytecode,
                &try_patch_runtime_toy_box_mag_assemble_help_bytecode,
                &try_patch_runtime_toy_box_eight_bit_blast_help_bytecode,
            };
        constexpr std::array<std::wstring_view, runtime_toy_box_tutorial_variant_count>
            s_toy_box_tutorial_labels{
                STR("aerial_recovery"),
                STR("gigas"),
                STR("mag_assemble"),
                STR("eight_bit_blast"),
            };
        constexpr std::array<size_t, runtime_toy_box_tutorial_variant_count>
            s_toy_box_tutorial_expected_script_sizes{
                runtime_toy_box_aerial_recovery_script_size,
                runtime_toy_box_gigas_script_size,
                runtime_toy_box_mag_assemble_script_size,
                runtime_toy_box_eight_bit_blast_script_size,
            };

        // Only resolves a function for unfinished work in the matching context.
        // The pointer is consumed synchronously and is never retained by lifecycle state.
        struct TutorialFunctionReadiness
        {
            UFunction* function_object{};
            bool requested{false};
        };

        [[nodiscard]] auto find_pending_tutorial_function(const TutorialPatchLifecycle& lifecycle,
                                                          bool enabled,
                                                          std::wstring_view context_hint,
                                                          std::wstring_view context_prefix,
                                                          std::wstring_view function_path)
            -> TutorialFunctionReadiness
        {
            if (!lifecycle.should_attempt(enabled, context_hint, context_prefix))
            {
                return {};
            }
            UFunction* const function_object = find_live_function(function_path);
            return {function_object, true};
        }

        [[nodiscard]] auto try_patch_toy_box_function(size_t variant_index,
                                                      std::wstring_view function_path,
                                                      UObject* object = nullptr)
            -> TutorialPatchAttempt
        {
            if (variant_index >= runtime_toy_box_tutorial_variant_count)
            {
                return {};
            }
            return try_patch_tutorial_function_object(
                find_constructed_or_live_function(object, function_path),
                s_toy_box_tutorial_patchers[variant_index]);
        }
    } // namespace

    RuntimeTutorialSkipService::RuntimeTutorialSkipService(
        RuntimeTutorialSkipConfig config) noexcept
        : config_(config)
    {
    }

    auto RuntimeTutorialSkipService::reset_playable_context() noexcept -> void
    {
        informational_help_.fill({});
        links_.fill({});
        olympus_completion_.reset();
        olympus_early_.reset();
        olympus_save_points_.reset();
        caribbean_sea_spectacle_.reset();
        toy_box_.fill({});
        twilight_town_.reset();
        final_world_.reset();
    }

    auto RuntimeTutorialSkipService::try_apply_for_context(std::wstring_view context_hint,
                                                           std::wstring_view source) -> void
    {
        try_apply_informational_help_for_context(context_hint, source);
        try_apply_links_for_context(context_hint, source);
        try_apply_olympus_completion_for_context(context_hint, source);
        try_apply_olympus_early_for_context(context_hint, source);
        try_apply_olympus_save_points_for_context(context_hint, source);
        try_apply_caribbean_sea_spectacle_for_context(context_hint, source);
        try_apply_toy_box_for_context(context_hint, source);
        try_apply_twilight_town_for_context(context_hint, source);
        try_apply_final_world_for_context(context_hint, source);
    }

    auto RuntimeTutorialSkipService::try_apply_links_for_context(std::wstring_view context_hint,
                                                                 std::wstring_view source) -> void
    {
        if (!config_.link_tutorials_enabled)
        {
            return;
        }
        for (size_t index = 0; index < runtime_link_tutorial_count; ++index)
        {
            const auto readiness = find_pending_tutorial_function(
                links_[index], true, context_hint, runtime_link_tutorial_context_prefixes[index],
                runtime_link_tutorial_function_paths[index]);
            UFunction* const function = readiness.function_object;
            if (!function)
            {
                continue; // A streamed gameflow can appear after map-ready.
            }
            auto& script = function->GetScript();
            const auto result = try_patch_runtime_link_tutorial_bytecode(
                script.GetData(), script.Num() > 0 ? static_cast<size_t>(script.Num()) : 0,
                static_cast<RuntimeLinkTutorialVariant>(index));
            links_[index].record_result(result != RuntimeTutorialPopupBypassResult::Rejected);
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] LinkTutorialBypass class={} "
                                                "result={} source={} script_size={}\n"),
                                            runtime_link_tutorial_class_names[index],
                                            tutorial_popup_result_name(result), source,
                                            script.Num());
        }
    }

    auto try_apply_runtime_link_tutorial_early_bypass(RuntimeLinkTutorialVariant variant,
                                                      UObject* object) -> bool
    {
        const auto index = static_cast<size_t>(variant);
        if (index >= runtime_link_tutorial_count)
        {
            return false;
        }
        try
        {
            UFunction* const function = find_constructed_or_live_function(
                object, runtime_link_tutorial_function_paths[index]);
            if (!function)
            {
                return false;
            }
            auto& script = function->GetScript();
            const auto result = try_patch_runtime_link_tutorial_bytecode(
                script.GetData(), script.Num() > 0 ? static_cast<size_t>(script.Num()) : 0,
                variant);
            if (result != RuntimeTutorialPopupBypassResult::AlreadyPatched)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] LinkTutorialEarlyPatch class={} result={} script_size={} "
                        "expected_size={} timing=uobject_create_pre_begin_play\n"),
                    runtime_link_tutorial_class_names[index], tutorial_popup_result_name(result),
                    script.Num(), runtime_link_tutorial_profiles[index].script_size);
            }
            return result != RuntimeTutorialPopupBypassResult::Rejected;
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] LinkTutorialEarlyPatch class={} exception\n"),
                runtime_link_tutorial_class_names[index]);
            return false;
        }
    }

    auto RuntimeTutorialSkipService::try_apply_informational_help_for_context(
        std::wstring_view context_hint, std::wstring_view source) -> void
    {
        if (!config_.informational_help_enabled)
        {
            return;
        }
        for (size_t index = 0; index < runtime_informational_help_class_names.size(); ++index)
        {
            const auto readiness =
                find_pending_tutorial_function(informational_help_[index], true, context_hint,
                                               runtime_informational_help_context_prefixes[index],
                                               runtime_informational_help_function_paths[index]);
            UFunction* const function = readiness.function_object;
            if (!function)
            {
                continue; // A streamed gameflow can appear after map-ready.
            }
            auto& script = function->GetScript();
            const auto result = try_patch_runtime_informational_help_bytecode(
                script.GetData(), script.Num() > 0 ? static_cast<size_t>(script.Num()) : 0, index);
            informational_help_[index].record_result(result !=
                                                     RuntimeTutorialPopupBypassResult::Rejected);
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] InformationalHelpBypass class={} "
                                                "result={} source={} script_size={}\n"),
                                            runtime_informational_help_class_names[index],
                                            tutorial_popup_result_name(result), source,
                                            script.Num());
        }
        if (informational_help_[5].applied() &&
            context_hint.find(L"/Game/Levels/ts/ts_02/") != context_hint.npos)
        {
            disable_cactuar_camera_directors();
            constexpr std::array<std::wstring_view, 3> callbacks{
                L"BndEvt__g_ts_Gigas_blockA26_K2Node_ActorBoundEvent_232_"
                L"TresReactorDoCommandSignature__DelegateSignature",
                L"BndEvt__g_ts_Gigas_blockB_C_74_K2Node_ActorBoundEvent_251_"
                L"TresReactorDoCommandSignature__DelegateSignature",
                L"BndEvt__g_ts_Gigas_blockC_C_105_K2Node_ActorBoundEvent_214_"
                L"TresReactorDoCommandSignature__DelegateSignature"};
            for (size_t part = 0; part < callbacks.size(); ++part)
            {
                const std::wstring level =
                    part == 0 ? L"ts_02_Denjigimmickfront" : L"ts_02_DenjigimmickBack";
                const std::wstring prefix =
                    L"/Game/Levels/ts/ts_02/umap/" + level + L"." + level + L"_C:";
                auto* owner = find_live_function(prefix + L"ExecuteUbergraph_" + level);
                auto* callback = find_live_function(prefix + std::wstring{callbacks[part]});
                if (!owner || !callback)
                {
                    continue;
                }
                auto& owner_script = owner->GetScript();
                auto& callback_script = callback->GetScript();
                const auto result = try_patch_cactuar_reactor_delegate(
                    callback_script.GetData(), callback_script.Num(), owner_script.GetData(),
                    owner_script.Num(), part);
                if (result != RuntimeTutorialPopupBypassResult::AlreadyPatched)
                {
                    Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] CactuarReaction part={} "
                                                        "result={} stop_on_interact=true\n"),
                                                    part, tutorial_popup_result_name(result));
                }
            }
        }
    }

    auto try_apply_runtime_informational_help_early_bypass(size_t variant, UObject* object) -> bool
    {
        const auto index = static_cast<size_t>(variant);
        if (index >= runtime_informational_help_class_names.size())
        {
            return false;
        }
        try
        {
            UFunction* const function = find_constructed_or_live_function(
                object, runtime_informational_help_function_paths[index]);
            if (!function)
            {
                return false;
            }
            auto& script = function->GetScript();
            const auto result = try_patch_runtime_informational_help_bytecode(
                script.GetData(), script.Num() > 0 ? static_cast<size_t>(script.Num()) : 0,
                variant);
            if (result != RuntimeTutorialPopupBypassResult::AlreadyPatched)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] InformationalHelpEarlyPatch class={} result={} "
                        "script_size={} timing=uobject_create_pre_begin_play\n"),
                    runtime_informational_help_class_names[index],
                    tutorial_popup_result_name(result), script.Num());
            }
            return result != RuntimeTutorialPopupBypassResult::Rejected;
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] InformationalHelpEarlyPatch class={} exception\n"),
                runtime_informational_help_class_names[index]);
            return false;
        }
    }

    auto RuntimeTutorialSkipService::final_world_ui_bypasses_applied() const noexcept -> bool
    {
        return final_world_.applied();
    }

    auto RuntimeTutorialSkipService::try_apply_olympus_completion_for_context(
        std::wstring_view context_hint, std::wstring_view source) -> void
    {
        const auto readiness = find_pending_tutorial_function(
            olympus_completion_, config_.olympus_completion_enabled, context_hint,
            config_.olympus_completion_context_prefix, config_.olympus_completion_function_path);
        UFunction* const function_object = readiness.function_object;
        if (!function_object)
        {
            if (readiness.requested)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusCompletionTutorialBypass function unavailable "
                        "source={}\n"),
                    source);
            }
            return;
        }

        const auto [script, script_size] = tutorial_function_script(*function_object);
        const RuntimeOlympusTutorialBypassResult tutorial_result =
            try_patch_runtime_olympus_tutorial_bytecode(script, script_size);
        const auto [shotlock_result, team_attacks_result, airstepping_result, ability_sets_result] =
            apply_tutorial_patch_batch(script, script_size, s_olympus_completion_patchers);
        if (tutorial_result != RuntimeOlympusTutorialBypassResult::Rejected &&
            shotlock_result != RuntimeTutorialPopupBypassResult::Rejected &&
            team_attacks_result != RuntimeTutorialPopupBypassResult::Rejected &&
            airstepping_result != RuntimeTutorialPopupBypassResult::Rejected &&
            ability_sets_result != RuntimeTutorialPopupBypassResult::Rejected)
        {
            olympus_completion_.record_result(true);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] OlympusHe01TutorialBypasses completion_result={} "
                    "shotlock_result={} team_attacks_result={} airstepping_result={} "
                    "ability_sets_result={} source={} script_size={} function={}\n"),
                olympus_tutorial_result_name(tutorial_result),
                tutorial_popup_result_name(shotlock_result),
                tutorial_popup_result_name(team_attacks_result),
                tutorial_popup_result_name(airstepping_result),
                tutorial_popup_result_name(ability_sets_result), source, script_size,
                config_.olympus_completion_function_path);
            return;
        }

        // Completion historically retries rejected batches on later lifecycle events.
        // Keep it pending rather than using the terminal rejection policy below.
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] OlympusHe01TutorialBypasses rejected unexpected bytecode "
                "completion_result={} shotlock_result={} team_attacks_result={} "
                "airstepping_result={} ability_sets_result={} source={} script_size={} "
                "expected_size={} function={}\n"),
            olympus_tutorial_result_name(tutorial_result),
            tutorial_popup_result_name(shotlock_result),
            tutorial_popup_result_name(team_attacks_result),
            tutorial_popup_result_name(airstepping_result),
            tutorial_popup_result_name(ability_sets_result), source, script_size,
            runtime_olympus_tutorial_script_size, config_.olympus_completion_function_path);
    }

    auto RuntimeTutorialSkipService::try_apply_olympus_early_for_context(
        std::wstring_view context_hint, std::wstring_view source) -> void
    {
        const auto readiness = find_pending_tutorial_function(
            olympus_early_, config_.olympus_early_tutorials_enabled, context_hint,
            config_.olympus_early_context_prefix, config_.olympus_early_function_path);
        UFunction* const function_object = readiness.function_object;
        if (!function_object)
        {
            if (readiness.requested && olympus_early_.note_unavailable())
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusEarlyTutorialBypasses function unavailable; "
                        "retrying on map-ready event source={} context={}\n"),
                    source, truncate_for_log(std::wstring{context_hint}, 260));
            }
            return;
        }

        const auto [script, script_size] = tutorial_function_script(*function_object);
        const auto [casting_magic_result, free_running_result, flowmotion_help_result,
                    attractions_help_result, ability_sets_result] =
            apply_tutorial_patch_batch(script, script_size, s_olympus_early_patchers);

        if (casting_magic_result != RuntimeTutorialPopupBypassResult::Rejected &&
            free_running_result != RuntimeTutorialPopupBypassResult::Rejected &&
            flowmotion_help_result != RuntimeTutorialPopupBypassResult::Rejected &&
            attractions_help_result != RuntimeTutorialPopupBypassResult::Rejected &&
            ability_sets_result != RuntimeTutorialPopupBypassResult::Rejected)
        {
            olympus_early_.record_result(true);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] OlympusEarlyTutorialBypasses casting_magic_result={} "
                    "free_running_result={} flowmotion_help_result={} attractions_help_result={} "
                    "ability_sets_result={} source={} script_size={} function={}\n"),
                tutorial_popup_result_name(casting_magic_result),
                tutorial_popup_result_name(free_running_result),
                tutorial_popup_result_name(flowmotion_help_result),
                tutorial_popup_result_name(attractions_help_result),
                tutorial_popup_result_name(ability_sets_result), source, script_size,
                config_.olympus_early_function_path);
            return;
        }

        olympus_early_.record_result(false);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] OlympusEarlyTutorialBypasses rejected unexpected bytecode "
                "casting_magic_result={} free_running_result={} flowmotion_help_result={} "
                "attractions_help_result={} ability_sets_result={} source={} script_size={} "
                "expected_size={} function={}\n"),
            tutorial_popup_result_name(casting_magic_result),
            tutorial_popup_result_name(free_running_result),
            tutorial_popup_result_name(flowmotion_help_result),
            tutorial_popup_result_name(attractions_help_result),
            tutorial_popup_result_name(ability_sets_result), source, script_size,
            runtime_olympus_early_tutorial_he02_script_size, config_.olympus_early_function_path);
    }

    auto RuntimeTutorialSkipService::try_apply_olympus_save_points_for_context(
        std::wstring_view context_hint, std::wstring_view source) -> void
    {
        const auto readiness = find_pending_tutorial_function(
            olympus_save_points_, config_.olympus_save_points_enabled, context_hint,
            config_.olympus_save_points_context_prefix, config_.olympus_save_points_function_path);
        UFunction* const function_object = readiness.function_object;
        if (!function_object)
        {
            if (readiness.requested && olympus_save_points_.note_unavailable())
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusHe03TutorialBypasses function unavailable; "
                        "retrying on map-ready event source={} context={}\n"),
                    source, truncate_for_log(std::wstring{context_hint}, 260));
            }
            return;
        }

        const auto [script, script_size] = tutorial_function_script(*function_object);
        const auto [flowmotion_tutorial_result, team_attacks_help_result, grand_magic_help_result,
                    formchange_tutorial_result, attractions_tutorial_result, ability_sets_result,
                    save_points_result, moogle_shop_result] =
            apply_tutorial_patch_batch(script, script_size, s_olympus_save_points_patchers);
        if (flowmotion_tutorial_result != RuntimeTutorialPopupBypassResult::Rejected &&
            team_attacks_help_result != RuntimeTutorialPopupBypassResult::Rejected &&
            grand_magic_help_result != RuntimeTutorialPopupBypassResult::Rejected &&
            formchange_tutorial_result != RuntimeTutorialPopupBypassResult::Rejected &&
            attractions_tutorial_result != RuntimeTutorialPopupBypassResult::Rejected &&
            ability_sets_result != RuntimeTutorialPopupBypassResult::Rejected &&
            save_points_result != RuntimeTutorialPopupBypassResult::Rejected &&
            moogle_shop_result != RuntimeTutorialPopupBypassResult::Rejected)
        {
            olympus_save_points_.record_result(true);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] OlympusHe03TutorialBypasses flowmotion_result={} "
                    "team_attacks_result={} grand_magic_result={} formchange_result={} "
                    "attractions_result={} ability_sets_result={} save_points_result={} "
                    "moogle_shop_result={} source={} script_size={} function={}\n"),
                tutorial_popup_result_name(flowmotion_tutorial_result),
                tutorial_popup_result_name(team_attacks_help_result),
                tutorial_popup_result_name(grand_magic_help_result),
                tutorial_popup_result_name(formchange_tutorial_result),
                tutorial_popup_result_name(attractions_tutorial_result),
                tutorial_popup_result_name(ability_sets_result),
                tutorial_popup_result_name(save_points_result),
                tutorial_popup_result_name(moogle_shop_result), source, script_size,
                config_.olympus_save_points_function_path);
            return;
        }

        olympus_save_points_.record_result(false);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] OlympusHe03TutorialBypasses rejected unexpected bytecode "
                "flowmotion_result={} team_attacks_result={} grand_magic_result={} "
                "formchange_result={} attractions_result={} ability_sets_result={} "
                "save_points_result={} moogle_shop_result={} source={} script_size={} "
                "expected_size={} function={}\n"),
            tutorial_popup_result_name(flowmotion_tutorial_result),
            tutorial_popup_result_name(team_attacks_help_result),
            tutorial_popup_result_name(grand_magic_help_result),
            tutorial_popup_result_name(formchange_tutorial_result),
            tutorial_popup_result_name(attractions_tutorial_result),
            tutorial_popup_result_name(ability_sets_result),
            tutorial_popup_result_name(save_points_result),
            tutorial_popup_result_name(moogle_shop_result), source, script_size,
            runtime_olympus_he03_tutorial_script_size, config_.olympus_save_points_function_path);
    }

    auto RuntimeTutorialSkipService::try_apply_caribbean_sea_spectacle_for_context(
        std::wstring_view context_hint, std::wstring_view source) -> void
    {
        const auto readiness = find_pending_tutorial_function(
            caribbean_sea_spectacle_,
            config_.caribbean_sea_spectacle_enabled &&
                !config_.caribbean_sea_spectacle_context_prefix.empty() &&
                !config_.caribbean_sea_spectacle_function_path.empty(),
            context_hint, config_.caribbean_sea_spectacle_context_prefix,
            config_.caribbean_sea_spectacle_function_path);
        if (!readiness.requested)
        {
            return;
        }

        const TutorialPatchAttempt attempt = try_patch_tutorial_function_object(
            readiness.function_object, &try_patch_runtime_caribbean_sea_spectacle_help_bytecode);
        if (!attempt.function_object)
        {
            if (caribbean_sea_spectacle_.note_unavailable())
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] CaribbeanSeaSpectacleTutorialBypass function unavailable; "
                        "retrying on later map-ready event source={} context={}\n"),
                    source, truncate_for_log(std::wstring{context_hint}, 260));
            }
            return;
        }

        if (attempt.accepted())
        {
            caribbean_sea_spectacle_.record_result(true);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] CaribbeanSeaSpectacleTutorialBypass result={} popup=removed "
                    "delay=preserved cinematic_cleanup=preserved ca_island_event3_logic=untouched "
                    "source={} script_size={} function={}\n"),
                tutorial_popup_result_name(attempt.result), source, attempt.script_size,
                config_.caribbean_sea_spectacle_function_path);
            return;
        }

        caribbean_sea_spectacle_.record_result(false);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] CaribbeanSeaSpectacleTutorialBypass rejected unexpected bytecode "
                "result={} source={} script_size={} expected_size={} function={}\n"),
            tutorial_popup_result_name(attempt.result), source, attempt.script_size,
            runtime_caribbean_sea_spectacle_script_size,
            config_.caribbean_sea_spectacle_function_path);
    }

    auto RuntimeTutorialSkipService::try_apply_toy_box_for_context(std::wstring_view context_hint,
                                                                   std::wstring_view source) -> void
    {
        if (!config_.toy_box_tutorials_enabled)
        {
            return;
        }

        for (size_t index = 0; index < runtime_toy_box_tutorial_variant_count; ++index)
        {
            const std::wstring_view context_prefix = config_.toy_box_context_prefixes[index];
            const auto readiness = find_pending_tutorial_function(
                toy_box_[index], !context_prefix.empty(), context_hint, context_prefix,
                config_.toy_box_function_paths[index]);
            if (!readiness.requested)
            {
                continue;
            }

            const TutorialPatchAttempt attempt = try_patch_tutorial_function_object(
                readiness.function_object, s_toy_box_tutorial_patchers[index]);
            if (!attempt.function_object)
            {
                if (toy_box_[index].note_unavailable())
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ToyBoxTutorialBypass function unavailable tutorial={} "
                            "source={} context={}; retrying on later map-ready event\n"),
                        s_toy_box_tutorial_labels[index], source,
                        truncate_for_log(std::wstring{context_hint}, 260));
                }
                continue;
            }

            if (attempt.accepted())
            {
                toy_box_[index].record_result(true);
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] ToyBoxTutorialBypass tutorial={} result={} source={} "
                        "script_size={} function={}\n"),
                    s_toy_box_tutorial_labels[index], tutorial_popup_result_name(attempt.result),
                    source, attempt.script_size, config_.toy_box_function_paths[index]);
                continue;
            }

            toy_box_[index].record_result(false);
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ToyBoxTutorialBypass rejected unexpected bytecode tutorial={} "
                    "result={} source={} script_size={} expected_size={} function={}\n"),
                s_toy_box_tutorial_labels[index], tutorial_popup_result_name(attempt.result),
                source, attempt.script_size, s_toy_box_tutorial_expected_script_sizes[index],
                config_.toy_box_function_paths[index]);
        }
    }

    auto RuntimeTutorialSkipService::try_apply_twilight_town_for_context(
        std::wstring_view context_hint, std::wstring_view source) -> void
    {
        const auto readiness =
            find_pending_tutorial_function(twilight_town_,
                                           config_.twilight_town_tutorials_enabled &&
                                               !config_.twilight_town_context_prefix.empty() &&
                                               !config_.twilight_town_function_path.empty(),
                                           context_hint, config_.twilight_town_context_prefix,
                                           config_.twilight_town_function_path);
        UFunction* const function_object = readiness.function_object;
        if (!function_object)
        {
            if (readiness.requested && twilight_town_.note_unavailable())
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] TwilightTownTutorialBypasses function unavailable; "
                        "retrying on later map-ready event source={} context={}\n"),
                    source, truncate_for_log(std::wstring{context_hint}, 260));
            }
            return;
        }

        const auto [script, script_size] = tutorial_function_script(*function_object);
        const auto [taking_photos_result, lucky_emblems_result, links_result, meow_wow_result] =
            apply_tutorial_patch_batch(script, script_size, s_twilight_town_patchers);
        if (taking_photos_result != RuntimeTutorialPopupBypassResult::Rejected &&
            lucky_emblems_result != RuntimeTutorialPopupBypassResult::Rejected &&
            links_result != RuntimeTutorialPopupBypassResult::Rejected &&
            meow_wow_result != RuntimeTutorialPopupBypassResult::Rejected)
        {
            twilight_town_.record_result(true);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] TwilightTownTutorialBypasses taking_photos_result={} "
                    "lucky_emblems_result={} links_result={} meow_wow_result={} "
                    "camera_help_flag=preserved source={} script_size={} function={}\n"),
                tutorial_popup_result_name(taking_photos_result),
                tutorial_popup_result_name(lucky_emblems_result),
                tutorial_popup_result_name(links_result),
                tutorial_popup_result_name(meow_wow_result), source, script_size,
                config_.twilight_town_function_path);
            return;
        }

        twilight_town_.record_result(false);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] TwilightTownTutorialBypasses rejected unexpected bytecode "
                "taking_photos_result={} lucky_emblems_result={} links_result={} "
                "meow_wow_result={} camera_help_flag=preserved source={} script_size={} "
                "expected_size={} function={}\n"),
            tutorial_popup_result_name(taking_photos_result),
            tutorial_popup_result_name(lucky_emblems_result),
            tutorial_popup_result_name(links_result), tutorial_popup_result_name(meow_wow_result),
            source, script_size, runtime_twilight_town_tutorial_script_size,
            config_.twilight_town_function_path);
    }

    auto
    RuntimeTutorialSkipService::try_apply_final_world_for_context(std::wstring_view context_hint,
                                                                  std::wstring_view source) -> void
    {
        if (!final_world_.should_attempt(
                config_.final_world_ui_enabled,
                is_runtime_final_world_guidance_bypass_context(context_hint)))
        {
            return;
        }

        bool found_function = false;
        bool applied_bypass = false;
        bool rejected_function = false;
        for (const std::wstring_view function_path : config_.final_world_function_paths)
        {
            const FinalWorldPatchAttempt attempt = try_patch_final_world_function(function_path);
            if (!attempt.function_object)
            {
                continue;
            }
            found_function = true;
            if (attempt.accepted())
            {
                applied_bypass = true;
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] FinalWorldBypasses guidance_result={} "
                        "combat_basics_result={} source={} script_size={} function={}\n"),
                    guidance_result_name(attempt.guidance_result),
                    tutorial_popup_result_name(attempt.combat_basics_result), source,
                    attempt.script_size, function_path);
                continue;
            }

            rejected_function = true;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] FinalWorldBypasses rejected unexpected bytecode "
                    "guidance_rejected={} combat_basics_rejected={} source={} script_size={} "
                    "expected_sizes={}/{} function={}\n"),
                attempt.guidance_result == RuntimeFinalWorldGuidanceBypassResult::Rejected,
                attempt.combat_basics_result == RuntimeTutorialPopupBypassResult::Rejected, source,
                attempt.script_size, runtime_final_world_guidance_ew01_script_size,
                runtime_final_world_guidance_ew04_script_size, function_path);
        }

        if (applied_bypass)
        {
            final_world_.record_result(true);
            return;
        }
        if (rejected_function)
        {
            final_world_.record_result(false);
            return;
        }
        if (!found_function && final_world_.note_unavailable())
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] FinalWorldBypasses functions unavailable; retrying on later "
                    "EW frames source={} context={}\n"),
                source, truncate_for_log(std::wstring{context_hint}, 260));
        }
    }

    auto try_apply_runtime_final_world_early_bypass(RuntimeFinalWorldGameflowVariant variant,
                                                    UObject* object) -> bool
    {
        const bool is_ew04 = variant == RuntimeFinalWorldGameflowVariant::Ew04;
        const std::wstring_view class_label =
            is_ew04 ? runtime_final_world_guidance_ew04_gameflow_class_name
                    : runtime_final_world_guidance_ew01_gameflow_class_name;
        const std::wstring_view function_path =
            is_ew04 ? runtime_final_world_guidance_ew04_function_path
                    : runtime_final_world_guidance_ew01_function_path;

        try
        {
            const FinalWorldPatchAttempt attempt =
                try_patch_final_world_function(function_path, object);
            if (!attempt.function_object)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] FinalWorldEarlyPatch function unavailable class={}\n"),
                    class_label);
                return false;
            }
            if (!attempt.accepted())
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] FinalWorldEarlyPatch rejected class={} script_size={} "
                        "guidance_rejected={} combat_basics_rejected={} function={}\n"),
                    class_label, attempt.script_size,
                    attempt.guidance_result == RuntimeFinalWorldGuidanceBypassResult::Rejected,
                    attempt.combat_basics_result == RuntimeTutorialPopupBypassResult::Rejected,
                    function_path);
                return false;
            }

            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] FinalWorldEarlyPatch guidance_result={} "
                    "combat_basics_result={} class={} script_size={} function={} "
                    "timing=uobject_create_pre_begin_play\n"),
                guidance_result_name(attempt.guidance_result),
                tutorial_popup_result_name(attempt.combat_basics_result), class_label,
                attempt.script_size, function_path);
            return true;
        }
        catch (const std::exception& exception)
        {
            const std::string message =
                exception.what() ? exception.what() : "<empty std::exception::what()>";
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] FinalWorldEarlyPatch exception={}\n"),
                std::wstring{message.begin(), message.end()});
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] FinalWorldEarlyPatch exception=<unknown>\n"));
        }
        return false;
    }

    auto try_apply_runtime_olympus_he02_early_bypass(UObject* gameflow_object) -> bool
    {
        try
        {
            UFunction* const class_function =
                gameflow_object && kh3ap::is_uobject_valid_fast(gameflow_object)
                    ? gameflow_object->GetFunctionByNameInChain(
                          runtime_olympus_early_execute_function_name.data())
                    : nullptr;
            const bool resolved_from_constructed_object =
                is_live_function_patch_target(class_function);
            UFunction* const function_object =
                resolved_from_constructed_object
                    ? class_function
                    : find_live_function(runtime_olympus_early_function_path);
            if (!function_object)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusHe02EarlyPatch function unavailable class={} "
                        "direct_chain_available={}\n"),
                    runtime_olympus_early_gameflow_class_name, class_function != nullptr);
                return false;
            }

            const auto [script, script_size] = tutorial_function_script(*function_object);
            const auto results =
                apply_tutorial_patch_batch(script, script_size, s_olympus_early_patchers);
            const auto result = combine_tutorial_patch_results(results);
            if (result == RuntimeTutorialPopupBypassResult::Rejected)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusHe02EarlyPatch rejected result={} class={} "
                        "script_size={} expected_size={} function={}\n"),
                    tutorial_popup_result_name(result), runtime_olympus_early_gameflow_class_name,
                    script_size, runtime_olympus_early_tutorial_he02_script_size,
                    runtime_olympus_early_function_path);
                return false;
            }

            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] OlympusHe02EarlyPatch result={} class={} script_size={} "
                    "function={} lookup={} timing=uobject_create_pre_begin_play\n"),
                tutorial_popup_result_name(result), runtime_olympus_early_gameflow_class_name,
                script_size, runtime_olympus_early_function_path,
                resolved_from_constructed_object ? STR("constructed_object_function_chain")
                                                 : STR("global_path_fallback"));
            return true;
        }
        catch (const std::exception& exception)
        {
            const std::string message =
                exception.what() ? exception.what() : "<empty std::exception::what()>";
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] OlympusHe02EarlyPatch exception={}\n"),
                std::wstring{message.begin(), message.end()});
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] OlympusHe02EarlyPatch exception=<unknown>\n"));
        }
        return false;
    }

    auto try_apply_runtime_twilight_town_early_bypass(UObject* gameflow_object) -> bool
    {
        try
        {
            UFunction* const class_function =
                gameflow_object && kh3ap::is_uobject_valid_fast(gameflow_object)
                    ? gameflow_object->GetFunctionByNameInChain(
                          runtime_twilight_town_execute_function_name.data())
                    : nullptr;
            const bool resolved_from_constructed_object =
                is_live_function_patch_target(class_function);
            UFunction* const function_object =
                resolved_from_constructed_object
                    ? class_function
                    : find_live_function(runtime_twilight_town_function_path);
            if (!function_object)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] TwilightTownEarlyPatch function unavailable class={} "
                        "direct_chain_available={}\n"),
                    runtime_twilight_town_gameflow_class_name, class_function != nullptr);
                return false;
            }

            const auto [script, script_size] = tutorial_function_script(*function_object);
            const auto results =
                apply_tutorial_patch_batch(script, script_size, s_twilight_town_patchers);
            const auto result = combine_tutorial_patch_results(results);
            if (result == RuntimeTutorialPopupBypassResult::Rejected)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] TwilightTownEarlyPatch rejected result={} class={} "
                        "script_size={} expected_size={} function={}\n"),
                    tutorial_popup_result_name(result), runtime_twilight_town_gameflow_class_name,
                    script_size, runtime_twilight_town_tutorial_script_size,
                    runtime_twilight_town_function_path);
                return false;
            }

            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] TwilightTownEarlyPatch result={} class={} script_size={} "
                    "function={} lookup={} timing=uobject_create_pre_begin_play\n"),
                tutorial_popup_result_name(result), runtime_twilight_town_gameflow_class_name,
                script_size, runtime_twilight_town_function_path,
                resolved_from_constructed_object ? STR("constructed_object_function_chain")
                                                 : STR("global_path_fallback"));
            return true;
        }
        catch (const std::exception& exception)
        {
            const std::string message =
                exception.what() ? exception.what() : "<empty std::exception::what()>";
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] TwilightTownEarlyPatch exception={}\n"),
                std::wstring{message.begin(), message.end()});
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] TwilightTownEarlyPatch exception=<unknown>\n"));
        }
        return false;
    }

    auto try_apply_runtime_world_keyblade_early_bypass(size_t index, UObject* object) -> bool
    {
        if (index >= runtime_world_keyblade_profiles.size())
        {
            return false;
        }
        try
        {
            const auto& profile = runtime_world_keyblade_profiles[index];
            const std::wstring name = L"ExecuteUbergraph_" + std::wstring{profile.gameflow};
            auto* function = is_uobject_valid_fast(object)
                                 ? object->GetFunctionByNameInChain(name.c_str())
                                 : nullptr;
            if (!is_live_function_patch_target(function))
            {
                return false;
            }
            auto* conversion = find_live_function(
                L"/Script/TresGame.TresGameBlueprintLibrary:BP_TresConvertItemIDtoKeyNameWeapon");
            auto* grant = find_live_function(
                L"/Script/TresGame.TresGameBlueprintLibrary:WaitBPTresGetKeyitem");
            if (!conversion || !grant)
            {
                return false;
            }
            const FName callback{name.c_str(), FNAME_Add};
            uint64_t callback_bits{};
            static_assert(sizeof(callback) == sizeof(callback_bits));
            std::memcpy(&callback_bits, &callback, sizeof(callback_bits));
            auto& script = function->GetScript();
            const auto result = try_patch_runtime_world_keyblade_bytecode(
                script.GetData(), script.Num() > 0 ? static_cast<size_t>(script.Num()) : 0,
                profile.weapon_id, reinterpret_cast<uint64_t>(conversion),
                reinterpret_cast<uint64_t>(grant), static_cast<uint32_t>(callback_bits));
            if (result == RuntimeWorldKeybladeBypassResult::NoMatchingGrant)
            {
                return true;
            }
            if (result != RuntimeWorldKeybladeBypassResult::AlreadyPatched)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] WorldKeybladeEarlyPatch gameflow={} weapon_id={} "
                        "script_size={} result={} timing=uobject_create_pre_begin_play\n"),
                    profile.gameflow, profile.weapon_id, script.Num(),
                    result == RuntimeWorldKeybladeBypassResult::Patched ? STR("patched")
                                                                        : STR("rejected"));
            }
            return result != RuntimeWorldKeybladeBypassResult::Rejected;
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] WorldKeybladeEarlyPatch exception index={}\n"), index);
            return false;
        }
    }

    auto try_apply_runtime_olympus_completion_early_bypass(UObject* gameflow_object) -> bool
    {
        try
        {
            UFunction* const class_function =
                gameflow_object && kh3ap::is_uobject_valid_fast(gameflow_object)
                    ? gameflow_object->GetFunctionByNameInChain(
                          runtime_olympus_completion_execute_function_name.data())
                    : nullptr;
            const bool resolved_from_constructed_object =
                is_live_function_patch_target(class_function);
            UFunction* const function_object =
                resolved_from_constructed_object
                    ? class_function
                    : find_live_function(runtime_olympus_completion_function_path);
            if (!function_object)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusHe01CompletionEarlyPatch function unavailable "
                        "class={} direct_chain_available={}\n"),
                    runtime_olympus_completion_gameflow_class_name, class_function != nullptr);
                return false;
            }

            const auto [script, script_size] = tutorial_function_script(*function_object);
            const RuntimeOlympusTutorialBypassResult result =
                try_patch_runtime_olympus_tutorial_bytecode(script, script_size);
            const auto popup_results =
                apply_tutorial_patch_batch(script, script_size, s_olympus_completion_patchers);
            if (result == RuntimeOlympusTutorialBypassResult::Rejected ||
                combine_tutorial_patch_results(popup_results) ==
                    RuntimeTutorialPopupBypassResult::Rejected)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusHe01CompletionEarlyPatch rejected result={} "
                        "class={} script_size={} expected_size={} function={}\n"),
                    olympus_tutorial_result_name(result),
                    runtime_olympus_completion_gameflow_class_name, script_size,
                    runtime_olympus_tutorial_script_size, runtime_olympus_completion_function_path);
                return false;
            }

            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] OlympusHe01CompletionEarlyPatch result={} class={} "
                    "script_size={} function={} lookup={} timing=uobject_create_pre_begin_play\n"),
                olympus_tutorial_result_name(result),
                runtime_olympus_completion_gameflow_class_name, script_size,
                runtime_olympus_completion_function_path,
                resolved_from_constructed_object ? STR("constructed_object_function_chain")
                                                 : STR("global_path_fallback"));
            return true;
        }
        catch (const std::exception& exception)
        {
            const std::string message =
                exception.what() ? exception.what() : "<empty std::exception::what()>";
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] OlympusHe01CompletionEarlyPatch exception={}\n"),
                std::wstring{message.begin(), message.end()});
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] OlympusHe01CompletionEarlyPatch exception=<unknown>\n"));
        }
        return false;
    }

    auto try_apply_runtime_olympus_save_points_early_bypass(UObject* object) -> bool
    {
        try
        {
            UFunction* const function_object = find_constructed_or_live_function(
                object, runtime_olympus_save_points_function_path);
            if (!function_object)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusHe03EarlyPatch function unavailable class={}\n"),
                    runtime_olympus_save_points_gameflow_class_name);
                return false;
            }

            const auto [script, script_size] = tutorial_function_script(*function_object);
            const auto [flowmotion_tutorial_result, team_attacks_help_result,
                        grand_magic_help_result, formchange_tutorial_result,
                        attractions_tutorial_result, ability_sets_result, save_points_result,
                        moogle_shop_result] =
                apply_tutorial_patch_batch(script, script_size, s_olympus_save_points_patchers);
            if (flowmotion_tutorial_result == RuntimeTutorialPopupBypassResult::Rejected ||
                team_attacks_help_result == RuntimeTutorialPopupBypassResult::Rejected ||
                grand_magic_help_result == RuntimeTutorialPopupBypassResult::Rejected ||
                formchange_tutorial_result == RuntimeTutorialPopupBypassResult::Rejected ||
                attractions_tutorial_result == RuntimeTutorialPopupBypassResult::Rejected ||
                ability_sets_result == RuntimeTutorialPopupBypassResult::Rejected ||
                save_points_result == RuntimeTutorialPopupBypassResult::Rejected ||
                moogle_shop_result == RuntimeTutorialPopupBypassResult::Rejected)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] OlympusHe03EarlyPatch rejected flowmotion_result={} "
                        "team_attacks_result={} grand_magic_result={} formchange_result={} "
                        "attractions_result={} ability_sets_result={} save_points_result={} "
                        "moogle_shop_result={} script_size={} expected_size={} function={}\n"),
                    tutorial_popup_result_name(flowmotion_tutorial_result),
                    tutorial_popup_result_name(team_attacks_help_result),
                    tutorial_popup_result_name(grand_magic_help_result),
                    tutorial_popup_result_name(formchange_tutorial_result),
                    tutorial_popup_result_name(attractions_tutorial_result),
                    tutorial_popup_result_name(ability_sets_result),
                    tutorial_popup_result_name(save_points_result),
                    tutorial_popup_result_name(moogle_shop_result), script_size,
                    runtime_olympus_he03_tutorial_script_size,
                    runtime_olympus_save_points_function_path);
                return false;
            }

            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] OlympusHe03EarlyPatch flowmotion_result={} "
                    "team_attacks_result={} grand_magic_result={} formchange_result={} "
                    "attractions_result={} ability_sets_result={} save_points_result={} "
                    "moogle_shop_result={} class={} script_size={} function={} "
                    "timing=uobject_create_pre_begin_play\n"),
                tutorial_popup_result_name(flowmotion_tutorial_result),
                tutorial_popup_result_name(team_attacks_help_result),
                tutorial_popup_result_name(grand_magic_help_result),
                tutorial_popup_result_name(formchange_tutorial_result),
                tutorial_popup_result_name(attractions_tutorial_result),
                tutorial_popup_result_name(ability_sets_result),
                tutorial_popup_result_name(save_points_result),
                tutorial_popup_result_name(moogle_shop_result),
                runtime_olympus_save_points_gameflow_class_name, script_size,
                runtime_olympus_save_points_function_path);
            return true;
        }
        catch (const std::exception& exception)
        {
            const std::string message =
                exception.what() ? exception.what() : "<empty std::exception::what()>";
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] OlympusHe03EarlyPatch exception={}\n"),
                std::wstring{message.begin(), message.end()});
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] OlympusHe03EarlyPatch exception=<unknown>\n"));
        }
        return false;
    }

    auto try_apply_runtime_caribbean_sea_spectacle_early_bypass(UObject* tutorial_actor) -> bool
    {
        try
        {
            UFunction* const class_function =
                tutorial_actor && kh3ap::is_uobject_valid_fast(tutorial_actor)
                    ? tutorial_actor->GetFunctionByNameInChain(
                          runtime_caribbean_sea_spectacle_execute_function_name.data())
                    : nullptr;
            const bool resolved_from_constructed_object =
                is_live_function_patch_target(class_function);
            UFunction* const function_object =
                resolved_from_constructed_object
                    ? class_function
                    : find_live_function(runtime_caribbean_sea_spectacle_function_path);
            const TutorialPatchAttempt attempt = try_patch_tutorial_function_object(
                function_object, &try_patch_runtime_caribbean_sea_spectacle_help_bytecode);
            if (!attempt.function_object)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] CaribbeanSeaSpectacleEarlyPatch function unavailable "
                        "class={} direct_chain_available={}\n"),
                    runtime_caribbean_sea_spectacle_class_name, class_function != nullptr);
                return false;
            }
            if (!attempt.accepted())
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] CaribbeanSeaSpectacleEarlyPatch rejected class={} "
                        "result={} script_size={} expected_size={} function={}\n"),
                    runtime_caribbean_sea_spectacle_class_name,
                    tutorial_popup_result_name(attempt.result), attempt.script_size,
                    runtime_caribbean_sea_spectacle_script_size,
                    runtime_caribbean_sea_spectacle_function_path);
                return false;
            }

            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] CaribbeanSeaSpectacleEarlyPatch result={} popup=removed "
                    "delay=preserved cinematic_cleanup=preserved ca_island_event3_logic=untouched "
                    "class={} script_size={} function={} timing=uobject_create_pre_begin_play "
                    "resolution={}\n"),
                tutorial_popup_result_name(attempt.result),
                runtime_caribbean_sea_spectacle_class_name, attempt.script_size,
                runtime_caribbean_sea_spectacle_function_path,
                resolved_from_constructed_object ? STR("constructed_object_function_chain")
                                                 : STR("global_path_fallback"));
            return true;
        }
        catch (const std::exception& exception)
        {
            const std::string message =
                exception.what() ? exception.what() : "<empty std::exception::what()>";
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] CaribbeanSeaSpectacleEarlyPatch exception={}\n"),
                std::wstring{message.begin(), message.end()});
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] CaribbeanSeaSpectacleEarlyPatch exception=<unknown>\n"));
        }
        return false;
    }

    auto try_apply_runtime_toy_box_early_bypass(RuntimeToyBoxTutorialVariant variant,
                                                UObject* object) -> bool
    {
        const size_t variant_index = static_cast<size_t>(variant);
        if (variant_index >= runtime_toy_box_tutorial_variant_count)
        {
            return false;
        }

        try
        {
            const TutorialPatchAttempt attempt = try_patch_toy_box_function(
                variant_index, runtime_toy_box_tutorial_function_paths[variant_index], object);
            if (!attempt.function_object)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ToyBoxEarlyPatch function unavailable tutorial={} "
                        "class={}\n"),
                    s_toy_box_tutorial_labels[variant_index],
                    runtime_toy_box_tutorial_gameflow_class_names[variant_index]);
                return false;
            }
            if (!attempt.accepted())
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ToyBoxEarlyPatch rejected tutorial={} class={} result={} "
                        "script_size={} expected_size={} function={}\n"),
                    s_toy_box_tutorial_labels[variant_index],
                    runtime_toy_box_tutorial_gameflow_class_names[variant_index],
                    tutorial_popup_result_name(attempt.result), attempt.script_size,
                    s_toy_box_tutorial_expected_script_sizes[variant_index],
                    runtime_toy_box_tutorial_function_paths[variant_index]);
                return false;
            }

            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] ToyBoxEarlyPatch tutorial={} result={} class={} "
                    "script_size={} function={} timing=uobject_create_pre_begin_play\n"),
                s_toy_box_tutorial_labels[variant_index],
                tutorial_popup_result_name(attempt.result),
                runtime_toy_box_tutorial_gameflow_class_names[variant_index], attempt.script_size,
                runtime_toy_box_tutorial_function_paths[variant_index]);
            return true;
        }
        catch (const std::exception& exception)
        {
            const std::string message =
                exception.what() ? exception.what() : "<empty std::exception::what()>";
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ToyBoxEarlyPatch tutorial={} exception={}\n"),
                s_toy_box_tutorial_labels[variant_index],
                std::wstring{message.begin(), message.end()});
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ToyBoxEarlyPatch tutorial={} exception=<unknown>\n"),
                s_toy_box_tutorial_labels[variant_index]);
        }
        return false;
    }
} // namespace kh3ap
