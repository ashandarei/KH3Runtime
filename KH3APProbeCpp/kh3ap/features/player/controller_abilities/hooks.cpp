#include "kh3ap/app/config.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/controller_abilities/host.h"

#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <array>
#include <stdexcept>
#include <string>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace controller_ability_detail;
    namespace
    {
        constexpr auto controller_setter_hook_count = 7;
        using ::kh3ap::settings::s_bp_ability_add_path;
        using ::kh3ap::settings::s_bp_ability_remove_path;
    } // namespace
    auto RuntimeControllerAbilityService::Impl::install_event_hooks() -> void
    {
        if (!event_hooks_.empty())
        {
            return;
        }
        // CharInit can contain AP_STYLE_CHANGE. Register its enum before
        // character assets load, without waiting for a playable pawn.
        ability_enum_augmentation_done_ = try_apply_ability_enum_augmentation();
        const std::array<std::wstring_view, 9> paths{setter_paths_[setter_index::wall_run],
                                                     setter_paths_[setter_index::style_change],
                                                     setter_paths_[setter_index::mp_enabled],
                                                     setter_paths_[setter_index::attraction_flow],
                                                     setter_paths_[setter_index::airstep],
                                                     setter_paths_[setter_index::magic_restriction],
                                                     setter_paths_[setter_index::links_restriction],
                                                     s_bp_ability_add_path,
                                                     s_bp_ability_remove_path};
        for (size_t index = 0; index < paths.size(); ++index)
        {
            try
            {
                const std::wstring path{paths[index]};
                const auto ids = UObjectGlobals::RegisterHook(
                    path,
                    [](UnrealScriptFunctionCallableContext&, void*)
                    {
                    },
                    [this, index](UnrealScriptFunctionCallableContext& context, void*)
                    {
                        if (RuntimeGrantInternalCallDepth::is_active())
                        {
                            return;
                        }
                        const auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
                        if (!context.Context ||
                            context.Context != (index < controller_setter_hook_count
                                                    ? pawn_state.controller_object()
                                                    : pawn_state.object()))
                        {
                            auto* aqua = persistent_player_reward_context();
                            auto* aqua_controller =
                                try_read_reflected_object_property_value(aqua, L"Controller");
                            if (aqua && index < ownership_setter_hook_count &&
                                context.Context == aqua_controller)
                            {
                                request_refresh(RuntimeControllerAbilityEvent::GameAbilityChanged);
                            }
                            return;
                        }
                        if (index == magic_restriction_hook_index ||
                            index == links_restriction_hook_index)
                        {
                            uint32_t flags{};
                            if (try_read_command_restrictions(pawn_state.object(), flags))
                            {
                                observe_restriction_pawn(pawn_state.object(), flags);
                                if (index == magic_restriction_hook_index)
                                {
                                    story_restricts_magic_ = (flags & restriction_bits::magic) != 0;
                                }
                                else
                                {
                                    story_restricts_links_ = (flags & restriction_bits::links) != 0;
                                }
                                applied_states_.invalidate(index == magic_restriction_hook_index
                                                               ? setter_index::magic_restriction
                                                               : setter_index::links_restriction);
                            }
                        }
                        else if (index < ownership_setter_hook_count)
                        {
                            applied_states_.invalidate(index);
                        }
                        config_.runtime_state.keyblade_style_state().force_apply = true;
                        request_refresh(RuntimeControllerAbilityEvent::GameAbilityChanged);
                    },
                    nullptr);
                if (ids.first == 0 && ids.second == 0)
                {
                    throw std::runtime_error("hook unavailable");
                }
                event_hooks_.emplace_back(path, ids);
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Controller ability event hook unavailable path={}\n"),
                    paths[index]);
            }
        }

        request_refresh(RuntimeControllerAbilityEvent::Startup);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Controller ability event hooks installed={}/{} polling=false\n"),
            event_hooks_.size(), paths.size());
    }

    auto RuntimeControllerAbilityService::Impl::uninstall_event_hooks() -> void
    {
        for (const auto& [path, ids] : event_hooks_)
        {
            try
            {
                UObjectGlobals::UnregisterHook(path, ids);
            }
            catch (...)
            {
            }
        }
        event_hooks_.clear();
    }
} // namespace kh3ap
