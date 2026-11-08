#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/features/world/garden_return/service_impl.h"
#include "kh3ap/core/ui/ui_layout.h"
#include "kh3ap/features/world/garden_return/runtime_context.h"
#include "kh3ap/features/world/garden_return/alternate_interaction_service.h"
#include "kh3ap/features/world/garden_return/character_restoration.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/characters/character_layout.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/characters/character_unlocks.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/combat/character_shotlock.h"
#include "kh3ap/features/player/combat/character_style.h"
#include "kh3ap/features/player/combat/aqua_style_unlock.h"
#include "kh3ap/features/player/characters/character_pawn_pool.h"
#include "kh3ap/features/player/characters/character_swap_assets.h"
#include "kh3ap/features/player/characters/character_swap_effects.h"
#include "kh3ap/features/player/combat/kairi_shining_hud.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/movement/character_barrel_spin.h"
#include "kh3ap/features/ingredients/ingredient_tracker.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json.hpp>
#include "kh3ap/diagnostics/character_swap_trace.h"
#endif
#include "kh3ap/features/player/characters/kairi_idle.h"
#include "kh3ap/features/player/movement/alternate_wall_run.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/world/garden_return/garden_return_service.h"
#include "kh3ap/features/world/garden_return/garden_return_policy.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/session/command_guard.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/bridge/transport/request_file_watcher.h"
#endif
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/combat/kh02_combat_swap.h"
#include "kh3ap/features/kh02/combat/kh02_combo_master.h"
#include "kh3ap/features/kh02/combat/kh02_companion_handoff.h"
#include "kh3ap/features/kh02/combat/kh02_mickey_link.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_interaction_policy.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/events/world_context_events.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include <Unreal/UClass.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstring>
#include <sstream>
#include <vector>
#include <tuple>
#include "kh3ap/features/player/characters/character_swap_timing.h"

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace garden_return_detail;

    namespace
    {
        bool garden_return_story_allowed(std::wstring_view current_world)
        {
            auto* owner = library();
            auto* fn = owner ? owner->GetFunctionByNameInChain(L"GetGameFlag") : nullptr;
            if (!is_uobject_valid_fast(fn) ||
                fn->GetParmsSize() != flag_integer_query_parameter_bytes)
            {
                return false;
            }
            auto* flag = find_function_param_by_name(fn, L"FlagName");
            auto* result = find_function_param_by_name(fn, L"ReturnValue");
            if (!flag || !result || result->GetClass().GetName() != L"IntProperty" ||
                result->GetOffset_Internal() != flag_query_result_offset ||
                result->GetElementSize() != sizeof(int32_t))
            {
                return false;
            }
            alignas(uint64_t) std::array<std::byte, flag_query_storage_bytes> params{};
            if (!write_name_or_string_function_param(flag, params.data(), L"gameflow_CA"))
            {
                return false;
            }
            owner->ProcessEvent(fn, params.data());
            if (field<int32_t>(params.data(), flag_query_result_offset) ==
                caribbean_return_blocked_story_state)
            {
                return false;
            }
            if (!camp_return_requires_corona_landing(current_world))
            {
                return true;
            }

            // RA_SAVE01 cannot be selected until this authored force-open
            // rule is reached. Leaving after the opening battle (RA 120)
            // otherwise strands the player at an empty Garden portal menu.
            auto* compare = owner->GetFunctionByNameInChain(L"GreaterEqual_FlagLabel");
            if (!is_uobject_valid_fast(compare) ||
                compare->GetParmsSize() != flag_comparison_parameter_bytes)
            {
                return false;
            }
            auto* comparison_result = find_function_param_by_name(compare, L"ReturnValue");
            if (!comparison_result || !comparison_result->IsA<FBoolProperty>() ||
                comparison_result->GetOffset_Internal() != flag_comparison_result_offset)
            {
                return false;
            }
            alignas(uint64_t) std::array<std::byte, flag_comparison_storage_bytes>
                comparison_params{};
            if (!write_name_or_string_function_param(find_function_param_by_name(compare, L"Flag"),
                                                     comparison_params.data(), L"gameflow_RA") ||
                !write_name_or_string_function_param(find_function_param_by_name(compare, L"label"),
                                                     comparison_params.data(), L"RA_FOREST_EVENT4"))
            {
                return false;
            }
            owner->ProcessEvent(compare, comparison_params.data());
            const bool first_save_point_available =
                static_cast<FBoolProperty*>(comparison_result)
                    ->GetPropertyValueInContainer(comparison_params.data());
            return camp_return_corona_landing_allowed(current_world, first_save_point_available);
        }

        bool jump_garden()
        {
            auto* owner = library();
            auto* fn = owner ? owner->GetFunctionByNameInChain(L"TresMapJump") : nullptr;
            if (!is_uobject_valid_fast(fn) ||
                fn->GetParmsSize() < map_jump_minimum_parameter_bytes ||
                fn->GetParmsSize() > map_jump_maximum_parameter_bytes)
            {
                return false;
            }
            alignas(uint64_t) std::array<std::byte, map_jump_maximum_parameter_bytes> params{};
            if (!write_name_or_string_function_param(find_function_param_by_name(fn, L"MapName"),
                                                     params.data(), space::garden) ||
                !write_name_or_string_function_param(find_function_param_by_name(fn, L"TargetTag"),
                                                     params.data(), space::garden_start) ||
                !write_integral_function_param(find_function_param_by_name(fn, L"InFadeKind"),
                                               params.data(), 1))
            {
                return false;
            }
            owner->ProcessEvent(fn, params.data());
            return true;
        }
    } // namespace

    void RuntimeGardenReturnService::Impl::close_camp(UObject* menu)
    {
        // Steam's normal Back handler owns close and completion. It also
        // respects cuisine mode; never synthesize pause/controller flags.
        if (steam)
        {
            reinterpret_cast<void (*)(UObject*)>(base + steam_camp_close_rva)(menu);
        }
        else
        {
            reinterpret_cast<Close>(base + egs_camp_close_rva)(menu, true);
        }
    }

    bool RuntimeGardenReturnService::Impl::change_player(UObject* pawn, int id)
    {
        if (!kh02_current_story_world())
        {
            return character_swap::dispatch_swap(pawn, id);
        }
        if ((id != sora_player_id && id != aqua_player_id) ||
            (player_id(pawn) != sora_player_id && player_id(pawn) != aqua_player_id))
        {
            return false;
        }
        auto* owner = library();
        auto* fn = owner ? owner->GetFunctionByNameInChain(L"BP_TresGameChangePlayer") : nullptr;
        if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() > max_camp_reflected_parameter_bytes)
        {
            return false;
        }
        std::vector<std::byte> params(fn->GetParmsSize());
        if (!write_object_function_param(fn, params.data(), L"WorldContextObject", pawn) ||
            !write_integral_function_param(find_function_param_by_name(fn, L"InKind"),
                                           params.data(), id))
        {
            return false;
        }
        if (id == aqua_player_id && player_id(pawn) == sora_player_id)
        {
            // The chapter owns a separate player factory. Keep the actual
            // outgoing Sora class and equipped weapon graphs resident here
            // too, so time on Aqua cannot turn a later R2 into a disk load.
            character_swap_assets::retain_return_class(pawn);
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        character_swap_trace::begin(id);
#endif
        {
            const CharacterSwapOperationTimer timer{L"kh02_release_wardrobe", id};
            Kh02Story::release_dressing_room(true);
        }
        {
            const CharacterSwapOperationTimer timer{L"kh02_companion_prepare", id};
            companion_handoff.prepare(pawn, world());
        }
        {
            const CharacterSwapOperationTimer timer{L"kh02_native_factory", id, true};
            // Close command submenus before the factory discards the outgoing
            // HUD state. This chapter uses its own factory instead of the
            // ordinary character-swap path, which already performs this reset.
            Kh02ReflectedCall reset_commands(owner, L"TresUI_ResetHudCommandState",
                                             "kh02_character_swap", max_camp_reflected_parameter_bytes);
            reset_commands.run();
            character_style::capture(pawn, id);
            owner->ProcessEvent(fn, params.data());
        }
        return true;
    }

    bool RuntimeGardenReturnService::Impl::character_key(int controller, std::wstring_view key,
                                                         int event, bool)
    {
        if (combat_swap.attack_key(controller, key, event))
        {
            return true;
        }
        if (key == L"Gamepad_FaceButton_Top" || key == L"F")
        {
            return interactions.route_save_key(controller, key, event);
        }
        if (key == L"Gamepad_LeftShoulder" || key == L"Gamepad_RightShoulder")
        {
            return character_input.route(controller, std::wstring(key), event,
                                         [&]
                                         {
                                             if (event != 0)
                                             {
                                                 return true;
                                             }
                                             auto* menu = camp();
                                             if (pending || !camp_is_kh02_world(world()) ||
                                                 !ready(menu) || !loading_closed() ||
                                                 Kh02Story::challenges_open())
                                             {
                                                 return false;
                                             }
                                             return key == L"Gamepad_LeftShoulder"
                                                        ? Kh02Story::open_challenges(menu)
                                                        : Kh02Story::open_dressing_room(menu);
                                         });
        }
        if (key != L"Gamepad_LeftTrigger" && key != L"Gamepad_RightTrigger")
        {
            return false;
        }
        return character_input.route(
            controller, std::wstring(key), event,
            [&]
            {
                auto* menu = camp();
                auto* pawn = player();
                if (event != 0)
                {
                    return true;
                }
                if (key == L"Gamepad_RightTrigger" && !ready(menu) && !pending &&
                    !interactions.save_pending() && !interactions.chest_pending() &&
                    loading_closed())
                {
                    return combat_swap.busy() || combat_swap.request(pawn);
                }
                if (pending || !camp_is_kh02_world(world()) || !ready(menu) || !loading_closed() ||
                    Kh02Story::challenges_open() ||
                    (player_id(pawn) != sora_player_id && player_id(pawn) != aqua_player_id))
                {
                    return false;
                }
                const int target = key == L"Gamepad_LeftTrigger" ? aqua_player_id : sora_player_id;
                if (player_id(pawn) == target)
                {
                    restoration.select_aqua(target == aqua_player_id);
                    return true;
                }
                pending_character = target;
                character_dispatched = false;
                source = world();
                selected_at = Clock::now();
                pending = true;
                phase = L"closing_camp_for_character";
                close_camp(menu);
                return true;
            });
    }

    RuntimeGardenReturnService::Impl::Impl(decltype(bridge) resolver) : bridge(std::move(resolver))
    {
    }

    RuntimeGardenReturnService::Impl::~Impl()
    {
        if (hook && hook->isHooked())
        {
            hook->unHook();
        }
        if (active == this)
        {
            active = nullptr;
        }
    }

    UObject* RuntimeGardenReturnService::Impl::camp()
    {
        auto* manager = object_field(object_field(viewport(), L"GameInstance"), L"m_UIManager");
        auto* controller = object_field(manager, L"m_CampController");
        if (!controller || field<uintptr_t>(controller, 0) != base + controller_vtable)
        {
            return nullptr;
        }
        auto* value = object_field(controller, L"m_CampMenu");
        return value && field<uintptr_t>(value, 0) == base + menu_vtable ? value : nullptr;
    }

    bool RuntimeGardenReturnService::Impl::ready(UObject* menu)
    {
        if (!is_uobject_valid_fast(menu) || field<uintptr_t>(menu, 0) != base + menu_vtable)
        {
            return false;
        }
        auto* controller = object_field(menu, L"m_CampController");
        return controller &&
               (field<uint32_t>(controller, camp_controller_enabled_flags_offset) & 1) &&
               field<uint8_t>(menu, native_ui::panel_open_offset) &&
               field<uint8_t>(menu, native_ui::panel_ready_offset) &&
               !field<uintptr_t>(menu, submenu_offset) && !field<uint8_t>(menu, busy_offset) &&
               !field<uint8_t>(menu, busy_offset + 1) &&
               !field<uint8_t>(menu, busy_offset + camp_third_busy_flag_delta);
    }

    bool RuntimeGardenReturnService::Impl::eligible()
    {
        const auto name = world();
        return !pending && camp_return_world_allowed(name) && loading_closed();
    }

    UObject* RuntimeGardenReturnService::Impl::stock(UObject* menu, int index)
    {
        if (!is_uobject_valid_fast(menu) || index < 0 || index >= stock_camp_row_count)
        {
            return nullptr;
        }
        // Fixed UPROPERTY array of nine proxies, not a TArray. Valid only
        // behind the verified storefront CampMenu vtable and executable guard.
        return object_field(field<UObject*>(menu, camp_rows_offset + sizeof(UObject*) * index),
                            L"m_ASObject");
    }

    UObject* RuntimeGardenReturnService::Impl::body(UObject* menu)
    {
        auto* last = stock(menu, last_stock_camp_row_index);
        return last ? try_gfx_get_member_value_object(gfx, last, L"parent") : nullptr;
    }

    UObject* RuntimeGardenReturnService::Impl::button(UObject* menu)
    {
        auto* parent = body(menu);
        return parent
                   ? try_gfx_invoke_string_arg_object(gfx, parent, L"getChildByName", garden_button)
                   : nullptr;
    }

    UObject* RuntimeGardenReturnService::Impl::clone_child(UObject* source_row,
                                                           UObject* source_child, UObject* row)
    {
        // The native proxy identifies the actual timeline instance names;
        // resolve the same relative path in the cloned display object.
        if (!source_row || !source_child || !row)
        {
            return nullptr;
        }
        const auto child_name = try_gfx_get_member_value_string(gfx, source_child, L"name");
        auto* parent = try_gfx_get_member_value_object(gfx, source_child, L"parent");
        const auto parent_name =
            parent ? try_gfx_get_member_value_string(gfx, parent, L"name") : std::nullopt;
        const auto root_name = try_gfx_get_member_value_string(gfx, source_row, L"name");
        if (!child_name || !parent_name || !root_name)
        {
            return nullptr;
        }
        auto* target_parent =
            *parent_name == *root_name
                ? row
                : try_gfx_invoke_string_arg_object(gfx, row, L"getChildByName", *parent_name);
        return target_parent ? try_gfx_invoke_string_arg_object(gfx, target_parent,
                                                                L"getChildByName", *child_name)
                             : nullptr;
    }

    void RuntimeGardenReturnService::Impl::install_button(UObject* menu)
    {
        if (editing || pending || !is_uobject_valid_fast(menu) ||
            field<uintptr_t>(menu, 0) != base + menu_vtable ||
            field<uintptr_t>(menu, submenu_offset))
        {
            return;
        }

        // Updating Scaleform navigation can synchronously emit focus events.
        struct EditingGuard
        {
            bool& flag;

            ~EditingGuard()
            {
                flag = false;
            }
        } guard{editing};

        editing = true;
        const bool to_space = space::camp_returns_to_space(world());
        auto* first = stock(menu, 0);
        auto* previous = stock(menu, last_stock_camp_row_index - 1);
        auto* last = stock(menu, last_stock_camp_row_index);
        auto* parent = body(menu);
        if (!first || !previous || !last || !parent)
        {
            return;
        }
        auto* row = try_gfx_invoke_string_arg_object(gfx, parent, L"getChildByName", garden_button);
        if (!row)
        {
            const auto index = try_gfx_get_member_value_number(gfx, last, L"index");
            if (!index || *index != camp_last_stock_row_anchor)
            {
                return;
            }
            // clone() registers its source index. Temporarily unregister it
            // so the new row cannot replace a stock FocusManager entry.
            if (!try_gfx_set_member_value_int(gfx, last, L"index", -1))
            {
                return;
            }
            row = try_gfx_invoke_no_args_object(gfx, last, L"clone");
            try_gfx_set_member_value_int(gfx, last, L"index", camp_last_stock_row_anchor);
            if (!row)
            {
                return;
            }
            row_enabled.reset();
            row_space.reset();
            row_kh02.reset();
            try_gfx_set_member_value_string(gfx, row, L"name", garden_button);
            try_gfx_set_member_value_int(gfx, row, L"index", garden_anchor);
            try_gfx_set_member_value_int(gfx, row, L"userParam", garden_anchor);
            try_gfx_set_member_value_string(gfx, row, L"textFieldPath",
                                            L"mainCmdNameAnm.mainCmdName");
            try_gfx_set_member_value_string(gfx, row, L"label",
                                            to_space                      ? L"Return to Space"
                                            : camp_is_kh02_world(world()) ? L"Return to GoA"
                                                                          : L"Return to Garden");
            try_gfx_set_member_value_bool(gfx, row, L"selected", false);
            if (!try_gfx_invoke_object_arg(gfx, parent, L"addChild", row))
            {
                return;
            }
            for (const auto* key : {L"x"})
            {
                if (const auto value = try_gfx_get_member_value_number(gfx, last, key))
                {
                    try_gfx_set_member_value_number(gfx, row, key, *value);
                }
            }
            // Fit ten rows into the stock nine-row area, including the
            // selected row's expanded help text at the bottom of the menu.
            for (int i = 0; i < expanded_camp_row_count; ++i)
            {
                auto* entry = i == stock_camp_row_count ? row : stock(menu, i);
                try_gfx_set_member_value_number(
                    gfx, entry, L"y",
                    camp_row_top + i * (camp_row_area_height / stock_camp_row_count));
                try_gfx_set_member_value_number(gfx, entry, L"scaleX", expanded_camp_row_scale);
                try_gfx_set_member_value_number(gfx, entry, L"scaleY", expanded_camp_row_scale);
            }
            auto* proxy = field<UObject*>(menu, camp_rows_offset +
                                                    sizeof(UObject*) * last_stock_camp_row_index);
            auto* help = clone_child(last, object_field(proxy, L"m_HelpMessage"), row);
            if (help)
            {
                try_gfx_set_member_value_string(gfx, help, L"text",
                                                to_space ? L"Return to the space world map."
                                                         : L"Return to the Garden of Assemblage.");
            }
            auto* icon = clone_child(last, object_field(proxy, L"m_CategoryIcon"), row);
            auto* key_icon =
                object_field(field<UObject*>(menu, camp_rows_offset), L"m_CategoryIcon");
            const auto frame = key_icon
                                   ? try_gfx_get_member_value_number(gfx, key_icon, L"currentFrame")
                                   : std::nullopt;
            if (icon && frame)
            {
                try_gfx_invoke_int_arg(gfx, icon, L"gotoAndStop", static_cast<int32_t>(*frame));
            }
            try_gfx_set_member_value_int(gfx, row, L"toUp", camp_last_stock_row_anchor);
            try_gfx_set_member_value_int(gfx, row, L"toDown", camp_first_row_anchor);
            try_gfx_set_member_value_int(gfx, row, L"toLeft", -1);
            try_gfx_set_member_value_int(gfx, row, L"toRight", -1);
            try_gfx_set_member_value_bool(gfx, row, L"repeatStopDown", true);
            try_gfx_set_member_value_bool(gfx, row, L"repeatStopUp", false);
            try_gfx_set_member_value_bool(gfx, row, L"visible", true);
            try_gfx_set_member_value_number(gfx, row, L"alpha", 1);
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] CampReturn added return row\n"));
        }
        // Native open/setup may rebuild stock navigation on each entry.
        try_gfx_set_member_value_int(gfx, last, L"toDown", garden_anchor);
        try_gfx_set_member_value_bool(gfx, last, L"repeatStopDown", false);
        try_gfx_set_member_value_int(gfx, first, L"toUp", garden_anchor);
        const bool in_kh02 = camp_is_kh02_world(world());
        if (!row_space || *row_space != to_space || !row_kh02 || *row_kh02 != in_kh02)
        {
            const bool label_set = try_gfx_set_member_value_string(gfx, row, L"label",
                                                                   to_space ? L"Return to Space"
                                                                   : camp_is_kh02_world(world())
                                                                       ? L"Return to GoA"
                                                                       : L"Return to Garden");
            auto* proxy = field<UObject*>(menu, camp_rows_offset +
                                                    sizeof(UObject*) * last_stock_camp_row_index);
            auto* help = clone_child(last, object_field(proxy, L"m_HelpMessage"), row);
            if (label_set && help &&
                try_gfx_set_member_value_string(gfx, help, L"text",
                                                to_space ? L"Return to the space world map."
                                                         : L"Return to the Garden of Assemblage."))
            {
                row_space = to_space;
                row_kh02 = in_kh02;
            }
        }
        // Read story progress only in this native menu event, including
        // each opening. Reopening restores the row after the story gate.
        const bool enabled = eligible() && (to_space || garden_return_story_allowed(world()));
        if (!row_enabled || *row_enabled != enabled)
        {
            // Button.enabled restarts its animation even for equal values.
            // Let the selected/disabled timeline run between state changes.
            try_gfx_set_member_value_bool(gfx, row, L"enabled", enabled);
            row_enabled = enabled;
        }
    }

    bool RuntimeGardenReturnService::Impl::select(UObject* menu)
    {
        if (!row_enabled.value_or(false) || !ready(menu) || !eligible() || !button(menu))
        {
            return false;
        }
        source = world();
        selected_at = Clock::now();
        pending = true;
        phase = L"closing_camp";
        ++selections;
        pending_character = -1;
        // Camp's normal close wrapper also broadcasts its controller's
        // completion delegate. Do not force open/pause bits or jump inside
        // the Scaleform callback while native UI teardown is on the stack.
        close_camp(menu);
        return true;
    }

    bool RuntimeGardenReturnService::Impl::input(UObject* menu, void* event)
    {
        auto& self = *active;
        const int32_t kind = field<int32_t>(event, native_ui::event_kind_offset);
        self.last_kind = kind;
        self.last_anchor = field<int32_t>(event, native_ui::event_anchor_offset);
        self.last_parameter = field<int32_t>(event, native_ui::event_parameter_offset);
        if (Kh02Story::challenges_open())
        {
            *reinterpret_cast<uint32_t*>(static_cast<std::byte*>(event) +
                                         native_ui::event_flags_offset) |=
                native_ui::event_handled_mask;
            return true;
        }
        if ((kind == native_ui::focus_event || kind == native_ui::decide_event) &&
            (field<int32_t>(event, native_ui::event_anchor_offset) == garden_anchor ||
             field<int32_t>(event, native_ui::event_parameter_offset) == garden_anchor))
        {
            if (kind == native_ui::decide_event)
            {
                self.select(menu);
            }
            *reinterpret_cast<uint32_t*>(static_cast<std::byte*>(event) +
                                         native_ui::event_flags_offset) |=
                native_ui::event_handled_mask;
            return true;
        }
        if (kind == native_ui::focus_event && self.last_anchor >= camp_first_row_anchor &&
            self.last_anchor <= camp_last_stock_row_anchor &&
            !field<uintptr_t>(menu, self.submenu_offset))
        {
            // Camp reuses its movie between worlds. Its native open path
            // clears only the nine stock rows, so clear our old highlight
            // when focus is assigned to a stock row on the next opening.
            if (auto* row = self.button(menu))
            {
                try_gfx_set_member_value_bool(gfx, row, L"selected", false);
            }
        }
        const bool result = reinterpret_cast<Input>(self.original)(menu, event);
        // Native setup assigns stock focus on each opening, including when
        // reusing the movie in another world. Update in that same event;
        // input-ready flags may still be unset during the opening animation.
        if (kind == native_ui::focus_event && !self.editing)
        {
            self.install_button(menu);
        }
        return result;
    }

    void RuntimeGardenReturnService::Impl::install()
    {
        if (attempted)
        {
            return;
        }
        attempted = true;
        base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return;
        }
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        constexpr std::array<uint8_t, 16> input_bytes{0x48, 0x89, 0x5c, 0x24, 0x18, 0x48,
                                                      0x89, 0x6c, 0x24, 0x20, 0x56, 0x57,
                                                      0x41, 0x57, 0x48, 0x83};
        constexpr std::array<uint8_t, 16> close_bytes{0x48, 0x89, 0x5c, 0x24, 0x08, 0x57,
                                                      0x48, 0x83, 0xec, 0x30, 0x48, 0x8b,
                                                      0x01, 0x0f, 0xb6, 0xda};
        const bool egs = kh3ap::native_build::egs.matches(nt->FileHeader.TimeDateStamp,
                                                          nt->OptionalHeader.SizeOfImage);
        steam = kh3ap::native_build::steam.matches(nt->FileHeader.TimeDateStamp,
                                                   nt->OptionalHeader.SizeOfImage);
        constexpr std::array<uint8_t, 21> steam_input{0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89,
                                                      0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24,
                                                      0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30};
        constexpr std::array<uint8_t, 19> steam_close{0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48,
                                                      0x8b, 0xd9, 0x48, 0x8b, 0x89, 0x18, 0x04,
                                                      0x00, 0x00, 0x48, 0x85, 0xc9};
        if (nt->Signature != IMAGE_NT_SIGNATURE || (!egs && !steam))
        {
            return;
        }
        if (steam)
        {
            if (std::memcmp(reinterpret_cast<void*>(base + steam_camp_input_rva),
                            steam_input.data(), steam_input.size()) ||
                std::memcmp(reinterpret_cast<void*>(base + steam_camp_close_rva),
                            steam_close.data(), steam_close.size()))
            {
                return;
            }
            input_rva = steam_camp_input_rva;
            controller_vtable = steam_camp_controller_vtable_rva;
            menu_vtable = steam_camp_menu_vtable_rva;
            submenu_offset = steam_camp_submenu_offset;
            busy_offset = steam_camp_busy_offset;
        }
        else if (std::memcmp(reinterpret_cast<void*>(base + input_rva), input_bytes.data(),
                             input_bytes.size()) ||
                 std::memcmp(reinterpret_cast<void*>(base + egs_camp_close_rva), close_bytes.data(),
                             close_bytes.size()))
        {
            return;
        }
        active = this;
        hook = std::make_unique<PLH::x64Detour>(base + input_rva,
                                                reinterpret_cast<uintptr_t>(&input), &original);
        installed = hook->hook() && original;
        if (!installed && hook->isHooked())
        {
            hook->unHook();
        }
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] CampReturn installed={} storefront={}\n"), installed,
            steam ? L"steam" : L"egs");
    }

    void RuntimeGardenReturnService::Impl::tick(Clock::time_point now)
    {
        if (!installed)
        {
            return;
        }
        {
            const RuntimeHotPathProfileScope profile{L"garden_return:combo_update"};
            combo_master.tick(
                world_context_revision.load(std::memory_order_acquire), now,
                [this]()
                {
                    return loading_closed() && !save_open();
                },
                [this]()
                {
                    return player();
                });
        }
        if (const auto selected = combat_swap.tick(now))
        {
            aqua_style_unlock::invalidate();
            if (kh02_current_story_world())
            {
                restoration.select_aqua(*selected == player_id_value(PlayerId::Aqua));
                restoration.request(restoration.snapshot().aqua_selected);
                if (*selected == 0)
                {
                    character_swap_assets::release_return_class();
                }
            }
            ++character_switches;
            phase = L"combat_character_change_observed";
        }
        if (character_style::pending() && !character_swap::handoff_pending())
        {
            character_style::restore(player());
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (!challenge_hold_key.empty() &&
            (now >= challenge_hold_release || !Kh02Story::challenges_open() ||
             world() != challenge_hold_world))
        {
            Kh02Story::route_challenges(0, challenge_hold_key, 1, challenge_hold_gamepad);
            challenge_hold_key.clear();
            challenge_hold_world.clear();
        }
#endif
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (is_uobject_valid_fast(save_press_controller.Get()) && now >= save_press_release)
        {
            // A diagnostic press must release even when no native save began.
            interactions.route_save_key(0, L"Gamepad_FaceButton_Top", 1);
            runtime_controller_action_input(save_press_controller.Get(), L"interact", false);
            save_press_controller = {};
        }
#endif
        interactions.tick_save(now);
        interactions.tick_interaction(now);
        restoration.tick(now);
        if (companion_handoff.pending())
        {
            companion_handoff.tick(
                [this]
                {
                    return player();
                },
                world());
        }
        mickey_link.tick(
            [this]
            {
                return player();
            },
            [this](UObject* pawn)
            {
                if (!change_player(pawn, aqua_player_id))
                {
                    return false;
                }
                restoration.select_aqua(true);
                restoration.request(restoration.snapshot().aqua_selected);
                ++character_switches;
                return true;
            },
            companion_handoff.pending());
        if (pending)
        {
            auto* menu = camp();
            if (world() != source || now - selected_at > garden_return_selection_timeout)
            {
                pending = false;
                phase = L"canceled_context_changed_or_timeout";
            }
            else if (menu && !field<uint8_t>(menu, native_ui::panel_open_offset) &&
                     !(field<uint32_t>(object_field(menu, L"m_CampController"),
                                       camp_controller_enabled_flags_offset) &
                       1) &&
                     loading_closed())
            {
                if (pending_character >= 0)
                {
                    auto* pawn = player();
                    if (!character_dispatched)
                    {
                        if (!change_player(pawn, pending_character))
                        {
                            pending = false;
                            phase = L"character_change_unavailable";
                        }
                        else
                        {
                            character_dispatched = true;
                            restoration.select_aqua(pending_character == aqua_player_id);
                            restoration.request(restoration.snapshot().aqua_selected);
                            phase = L"changing_character";
                        }
                    }
                    else if (player_id(pawn) == pending_character)
                    {
                        if (pending_character == sora_player_id)
                        {
                            character_swap_assets::release_return_class();
                        }
                        pending = false;
                        phase = L"character_change_observed";
                        ++character_switches;
                        aqua_style_unlock::invalidate();
                    }
                }
                else
                {
                    // Use the selected source, not a cached row label. Both
                    // destinations dispatch only after camp fully releases pause.
                    const bool to_space = space::camp_returns_to_space(source);
                    const bool jumped = to_space
                                            ? RuntimeSpaceService::return_from_closed_menu(source)
                                            : jump_garden();
                    // Space's native pause gate can clear after the camp bits;
                    // retain the bounded request until it accepts the dispatch.
                    if (jumped || !to_space)
                    {
                        pending = false;
                        phase = jumped ? L"jump_dispatched" : L"jump_unavailable";
                        Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] CampReturn phase={} source={}\n"), phase, source);
                    }
                }
            }
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (phone_key_down && now >= phone_key_release)
        {
            if (auto* controller = phone_key_controller.Get(); is_uobject_valid_fast(controller))
            {
                try
                {
                    runtime_controller_action_input(controller, phone_key_action, false);
                }
                catch (const std::exception& error)
                {
                    const std::string message = error.what();
                    phone_key_error = std::wstring(message.begin(), message.end());
                }
            }
            else
            {
                phone_key_error = L"gummiphone_controller_changed_before_release";
            }
            phone_key_down = false;
            phone_key_completed = phone_key_id;
            phone_key_controller = nullptr;
        }
        // Directory notifications wake new requests. Existing requests can
        // finish across frames (for example, a native key press/release).
        if (!diagnostic_requests.update(bridge(), true))
        {
            return;
        }
        const RuntimeHotPathProfileScope profile{L"garden_return:diagnostic_poll"};
        diagnostic();
#endif
    }

    RuntimeGardenReturnService::RuntimeGardenReturnService(
        std::function<std::optional<std::filesystem::path>()> bridge)
        : impl_(std::make_unique<Impl>(std::move(bridge)))
    {
    }

    RuntimeGardenReturnService::~RuntimeGardenReturnService() = default;

    void RuntimeGardenReturnService::install_once()
    {
        impl_->install();
    }

    void RuntimeGardenReturnService::tick(Clock::time_point now)
    {
        impl_->tick(now);
    }

    bool RuntimeGardenReturnService::request_experimental_swap(UObject* pawn)
    {
        return !character_swap::manual_swaps_blocked() && Impl::active &&
               character_swap::swap_available(pawn) &&
               (Impl::active->combat_swap.busy() || Impl::active->combat_swap.request(pawn));
    }

    bool RuntimeGardenReturnService::request_character_trap_swap(UObject* pawn)
    {
        return Impl::active && character_swap::manual_swaps_blocked() &&
               character_swap::swap_available(pawn) && !Impl::active->combat_swap.busy() &&
               Impl::active->combat_swap.request(pawn, true);
    }

    bool RuntimeGardenReturnService::finish_kh02_arrival(UObject* pawn)
    {
        return !Impl::active || Impl::active->restoration.finish_arrival(pawn);
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    void
    RuntimeGardenReturnService::set_gummiphone_diagnostic(std::function<std::string(int)> callback)
    {
        impl_->gummiphone_diagnostic = std::move(callback);
    }
#endif

    bool RuntimeGardenReturnService::route_kh02_character(int controller, std::wstring_view key,
                                                          int event, bool gamepad)
    {
        return Impl::active && Impl::active->character_key(controller, key, event, gamepad);
    }
} // namespace kh3ap
