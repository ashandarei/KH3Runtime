#include "service_impl.h"
#include "helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/ui/ui_layout.h"
#include "kh3ap/features/tutorials/tutorial_suppression.h"
#include "kh3ap/features/ingredients/ingredient_service.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace controller_input;
    using namespace ui_control_detail;
    void RuntimeUiControlService::Impl::advance_event_benchmark()
    {
        if (pawn_name != expected_pawn || !config.gameplay_ready() || pause_open ||
            !config.event_read_benchmark)
        {
            throw std::runtime_error("event_benchmark_requires_idle_sora");
        }
        remote_json = config.event_read_benchmark(config.sora());
        finish(L"completed", L"event_read_benchmark");
        return;
    }

    void RuntimeUiControlService::Impl::advance_ingredient_benchmark()
    {
        if (pawn_name != expected_pawn || !config.gameplay_ready() || pause_open)
        {
            throw std::runtime_error("ingredient_benchmark_requires_idle_sora");
        }
        remote_json = RuntimeIngredientService::benchmark_stock(config.sora());
        finish(L"completed", L"ingredient_stock_benchmark");
        return;
    }

    void RuntimeUiControlService::Impl::advance_bootstrap()
    {
        if (pawn_name != expected_pawn || !config.gameplay_ready() || pause_open ||
            !config.bootstrap_probe)
        {
            throw std::runtime_error("bootstrap_probe_requires_idle_sora");
        }
        bootstrap_json = config.bootstrap_probe(command == L"bootstrap_remove_ui");
        finish(L"completed", L"bootstrap_snapshot");
        return;
    }

    void RuntimeUiControlService::Impl::advance_viewport()
    {
        if (pawn_name != expected_pawn || !config.gameplay_ready() || pause_open)
        {
            throw std::runtime_error("viewport_requires_idle_sora");
        }
        viewport_json = viewport_fixture(
            config.sora(),
            command == L"viewport_set" ? std::optional{viewport_request} : std::nullopt,
            apply_viewport);
        finish(L"completed", L"viewport_snapshot");
        return;
    }

    void RuntimeUiControlService::Impl::advance_space_dialogue()
    {
        if (pause_open || pause_type != 0 ||
            !test_runtime_space_dialogue_suppression(manager_ref.Get()))
        {
            throw std::runtime_error(
                "space_dialogue_suppression_not_proven_requires_idle_overview");
        }
        dispatched = true;
        finish(L"completed", L"space_dialogue_suppressed_before_open_fixture_restored");
        return;
    }

    void RuntimeUiControlService::Impl::advance_tutorial()
    {
        const bool space = RuntimeSpaceService::is_space_active();
        auto* pawn = space ? RuntimeSpaceService::item_delivery_context() : config.sora();
        if (!is_uobject_valid_fast(pawn) || (!space && pawn->GetFullName() != expected_pawn))
        {
            throw std::runtime_error("tutorial_test_pawn_changed");
        }
        auto* manager = unique(L"TresUIManager");
        auto* help = try_read_reflected_object_property_value(manager, L"m_GameHelp");
        if (!is_uobject_valid_fast(help) ||
            help->GetClassPrivate()->GetName() != L"TresUIP_GameHelp")
        {
            throw std::runtime_error("tutorial_panel_unavailable");
        }
        if (dispatched)
        {
            opened = opened || field<uint8_t>(help, native_ui::panel_open_offset) != 0;
            if (opened)
            {
                throw std::runtime_error("tutorial_rendered_instead_of_suppressed");
            }
            if (runtime_suppressed_help_count() > tutorial_count_before &&
                runtime_suppressed_help_completion_count() > tutorial_completed_before &&
                pause_type == 0)
            {
                finish(L"completed", L"tutorial_suppressed_before_open_and_completion_observed");
            }
            return;
        }
        if ((!space && !config.gameplay_ready()) || pause_open || pause_type != 0 ||
            field<uint8_t>(help, native_ui::panel_open_offset))
        {
            throw std::runtime_error("tutorial_test_requires_idle_gameplay");
        }
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        auto* function = is_uobject_valid_fast(library)
                             ? library->GetFunctionByNameInChain(L"TresUI_OpenGameHelp")
                             : nullptr;
        if (!is_uobject_valid_fast(function) ||
            function->GetParmsSize() < minimum_command_key_parameter_bytes ||
            function->GetParmsSize() > maximum_command_key_parameter_bytes)
        {
            throw std::runtime_error("tutorial_open_layout_mismatch");
        }
        offset(function, L"WorldContextObject", 0);
        offset(function, L"LatentInfo", help_latent_info_offset);
        offset(function, L"HelpId", help_id_offset);
        offset(function, L"WithPause", help_with_pause_offset);
        alignas(uint64_t) std::array<std::byte, help_parameter_storage_bytes> params{};
        std::memcpy(params.data(), &pawn, sizeof(pawn));
        // No synthetic story callback: the diagnostic only observes
        // the real panel. Linkage -1 leaves the latent completion inert.
        const int32_t linkage = -1;
        std::memcpy(params.data() + help_latent_info_offset, &linkage, sizeof(linkage));
        params[help_id_offset] =
            space ? std::byte{space_overview_help_id}
                  : std::byte{graveyard_tutorial_help_id}; // Gummi overview or KG tutorial.
        params[help_with_pause_offset] =
            std::byte{1}; // Also prove that native completion releases its pause.
        tutorial_count_before = runtime_suppressed_help_count();
        tutorial_completed_before = runtime_suppressed_help_completion_count();
        library->ProcessEvent(function, params.data());
        dispatched = true;
        opened = field<uint8_t>(help, native_ui::panel_open_offset) != 0;
        return;
    }

    void RuntimeUiControlService::Impl::advance_magic_fixture()
    {
        auto* pawn = config.sora();
        if (!is_uobject_valid_fast(pawn) || pawn->GetFullName() != expected_pawn ||
            !config.gameplay_ready() || pause_open || pause_type != 0)
        {
            throw std::runtime_error("fixture_requires_expected_playable_sora");
        }
        std::array<uint32_t, 2> values{};
        if (!runtime_magic_fixture(pawn, values))
        {
            throw std::runtime_error("fixture_read_failed");
        }
        auto record = [&]()
        {
            magic_fixture_json =
                "{\"active\":" + std::to_string(values[0]) +
                ",\"staging\":" + std::to_string(values[1]) + ",\"backup_active\":" +
                (magic_fixture_backup ? std::to_string((*magic_fixture_backup)[0]) : "null") +
                ",\"backup_staging\":" +
                (magic_fixture_backup ? std::to_string((*magic_fixture_backup)[1]) : "null") + "}";
        };
        record();
        if (command != L"magic_fixture_inspect")
        {
            std::array<uint32_t, 2> target{};
            if (command == L"magic_fixture_disable")
            {
                RuntimeSoraAbilityEntryState links{};
                if (magic_fixture_backup ||
                    !try_read_runtime_sora_ability_entry_state(links_ability_id, pawn, &links) ||
                    !links.owned || !links.equipped)
                {
                    throw std::runtime_error("fixture_requires_links_and_no_active_fixture");
                }
                magic_fixture_backup = values;
            }
            else
            {
                if (!magic_fixture_backup)
                {
                    throw std::runtime_error("fixture_backup_missing");
                }
                target = *magic_fixture_backup;
            }
            record();
            report(L"accepted", L"fixture_checkpoint_before_write");
            const auto expected = values;
            if (!runtime_magic_fixture(pawn, values, &expected, &target))
            {
                throw std::runtime_error("fixture_write_failed_inspect_banks");
            }
            config.refresh_controller_abilities();
            record();
            if (command == L"magic_fixture_restore")
            {
                magic_fixture_backup.reset();
            }
        }
        finish(L"completed", L"magic_fixture_observed");
        return;
    }

    void RuntimeUiControlService::Impl::advance_command_step()
    {
        auto* pawn = cutscene_pawn();
        if (!is_uobject_valid_fast(pawn) || pawn->GetFullName() != expected_pawn ||
            command_pawn_identity(pawn) != expected_command_pawn_identity ||
            (dispatched && sora_ref.Get() != pawn))
        {
            throw std::runtime_error("command_pawn_changed");
        }
        offset(pawn, L"m_MagicPoint", sora_magic_point_offset);
        auto* state_comp = try_read_reflected_object_property_value(pawn, L"MyStateComp");
        auto* state = is_uobject_valid_fast(state_comp)
                          ? try_read_reflected_object_property_value(state_comp, L"m_pCurrentState")
                          : nullptr;
        const auto sample_value =
            std::pair{is_uobject_valid_fast(state) ? state->GetFullName() : L"",
                      field<int32_t>(pawn, sora_magic_point_offset)};
        if (command_samples.empty() || command_samples.back() != sample_value)
        {
            command_samples.push_back(sample_value);
        }
        if (!dispatched)
        {
            if (!config.gameplay_ready() || pause_open || pause_type != 0)
            {
                throw std::runtime_error("gameplay_not_ready");
            }
            command_json = command_snapshot(manager_ref.Get(), pawn);
            sora_ref = pawn;
            auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
            selection_controller = controller;
            if (selection_controller.Get() != controller)
            {
                throw std::runtime_error("command_controller_identity_unavailable");
            }
            runtime_controller_action_input(controller, command_action, true);
            command_key_down = dispatched = true;
            command_done = Clock::now() + command_key_hold_timeout;
            return;
        }
        if (command_key_down)
        {
            command_key_down = false;
            auto* controller = selection_controller.Get();
            if (!is_uobject_valid_fast(controller))
            {
                throw std::runtime_error("command_controller_changed");
            }
            runtime_controller_action_input(controller, command_action, false);
            command_released = true;
        }
        if (Clock::now() < command_done)
        {
            return;
        }
        command_json = command_snapshot(manager_ref.Get(), pawn);
        finish(L"completed", L"command_input_observed");
        return;
    }

    void RuntimeUiControlService::Impl::advance_command_inspect()
    {
        if (!config.gameplay_ready())
        {
            throw std::runtime_error("gameplay_not_ready");
        }
        auto* pawn = cutscene_pawn();
        if (!is_uobject_valid_fast(pawn) || pawn->GetFullName() != expected_pawn)
        {
            throw std::runtime_error("command_pawn_changed");
        }
        command_json = command_snapshot(manager_ref.Get(), pawn);
        finish(L"completed", L"command_snapshot");
        return;
    }

    void RuntimeUiControlService::Impl::advance_workshop_materials()
    {
        if (expected_pawn != pawn_name || pawn_name.empty() || !config.gameplay_ready() ||
            config.game_over_active())
        {
            throw std::runtime_error("workshop_requires_current_playable_sora");
        }
        auto* sora = config.sora();
        if (!config.stock_workshop_test_materials(sora))
        {
            throw std::runtime_error("workshop_test_stock_failed");
        }
        workshop_json = workshop_probe(base, false);
        finish(L"completed", L"test_materials_granted_to_sora");
        return;
    }

    void RuntimeUiControlService::Impl::advance_workshop()
    {
        if (expected_pawn != pawn_name || pawn_name.empty() || !config.gameplay_ready() ||
            config.game_over_active())
        {
            throw std::runtime_error("workshop_requires_current_playable_sora");
        }
        workshop_json =
            workshop_probe(base, command == L"synthesize_ether", command == L"workshop_deposit");
        finish(L"completed", L"native_workshop_transaction_observed");
        return;
    }

} // namespace kh3ap
