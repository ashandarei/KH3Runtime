#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/music/events/music_update_events.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;

    RuntimePlayerContextService::Impl::Impl(RuntimePlayerContextConfig value)
        : config(std::move(value))
    {
    }

    auto RuntimePlayerContextService::Impl::bridge_dir() const
        -> std::optional<std::filesystem::path>
    {
        return config.bridge_dir_resolver ? config.bridge_dir_resolver() : std::nullopt;
    }

    auto RuntimePlayerContextService::Impl::runtime_context() const -> std::wstring
    {
        return config.runtime_context_resolver ? config.runtime_context_resolver() : std::wstring{};
    }

    auto RuntimePlayerContextService::Impl::reset_playable_context() -> void
    {
        local_player = nullptr;
        if (!map_ready_context_key.empty())
        {
            request_music_update(MusicUpdate::Context);
        }
        map_ready_context_key.clear();
        map_ready_pawn = nullptr;
        map_ready_world = nullptr;
        config.tutorial_skips.reset_playable_context();
        final_world_capture_armed = false;
        final_world_capture_closed = false;
        final_world_capture_flag_failure_logged = false;
    }

    RuntimePlayerContextService::RuntimePlayerContextService(RuntimePlayerContextConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimePlayerContextService::~RuntimePlayerContextService() = default;
    RuntimePlayerContextService::RuntimePlayerContextService(
        RuntimePlayerContextService&&) noexcept = default;
    auto RuntimePlayerContextService::operator=(RuntimePlayerContextService&&) noexcept
        -> RuntimePlayerContextService& = default;

    auto RuntimePlayerContextService::reset_playable_context() -> void
    {
        impl_->reset_playable_context();
    }

    auto RuntimePlayerContextService::map_ready_context_key() const -> std::wstring_view
    {
        return impl_->map_ready_context_key;
    }

    auto RuntimePlayerContextService::final_world_capture_armed() const -> bool
    {
        return impl_->final_world_capture_armed;
    }

    auto RuntimePlayerContextService::arm_final_world_capture() -> void
    {
        impl_->final_world_capture_armed = true;
        impl_->final_world_capture_closed = false;
        impl_->final_world_capture_flag_failure_logged = false;
    }

    auto RuntimePlayerContextService::clear_special_battle_recovery_pending() -> void
    {
        impl_->special_battle_game_over_recovery_pending = false;
    }

    auto RuntimePlayerContextService::maybe_cache_blueprint_library_default_object(
        UObject* context, bool allow_cached_pointer_skip) -> void
    {
        if (!context)
        {
            return;
        }
        auto& object_cache = impl_->config.runtime_state.object_cache();
        if (allow_cached_pointer_skip &&
            object_cache.tres_game_blueprint_library_default_object() == context)
        {
            return;
        }
        if (kh3ap::is_uobject_valid_fast(context) &&
            context->GetFullName().find(STR("TresGameBlueprintLibrary")) != std::wstring::npos)
        {
            object_cache.set_tres_game_blueprint_library_default_object(context);
        }
    }

    auto RuntimePlayerContextService::is_player_controller_name(std::wstring_view object_name) const
        -> bool
    {
        return impl_->is_player_controller_name(object_name);
    }

    auto RuntimePlayerContextService::is_player_controller_object(UObject* object) const -> bool
    {
        return impl_->is_player_controller_object(object);
    }

    auto RuntimePlayerContextService::try_write_new_game_staging_from_controller_pawn(
        UObject* controller, std::wstring_view source) -> bool
    {
        return impl_->try_write_new_game_staging_from_controller_pawn(controller, source);
    }

    auto RuntimePlayerContextService::cache_pawn_candidate(UObject* pawn, std::wstring_view source)
        -> bool
    {
        return impl_->cache_pawn_candidate(pawn, source);
    }

    auto RuntimePlayerContextService::is_validated_sora_pawn_name(
        std::wstring_view pawn_name, std::wstring_view class_name) const -> bool
    {
        return impl_->is_validated_sora_pawn_name(pawn_name, class_name);
    }

    auto RuntimePlayerContextService::cache_validated_sora_pawn_candidate(
        UObject* pawn, std::wstring_view pawn_name, std::wstring_view source,
        bool allow_during_special_battle) -> bool
    {
        return impl_->cache_validated_sora_pawn_candidate(pawn, pawn_name, source,
                                                          allow_during_special_battle);
    }

    auto RuntimePlayerContextService::cache_constructed_sora_pawn(UObject* pawn,
                                                                  std::wstring_view source) -> bool
    {
        return impl_->cache_constructed_sora_pawn(pawn, source);
    }

    auto RuntimePlayerContextService::refresh_controller_from_sora_event(UObject* pawn) -> bool
    {
        return impl_->refresh_controller_from_sora_event(pawn);
    }

    auto RuntimePlayerContextService::try_capture_final_world_tutorial_pawn_once(
        UObject* world_context_object, std::wstring_view source, std::wstring_view caller) -> bool
    {
        return impl_->try_capture_final_world_tutorial_pawn_once(world_context_object, source,
                                                                 caller);
    }

    auto RuntimePlayerContextService::cache_pawn_from_object(UObject* object,
                                                             std::wstring_view source) -> bool
    {
        return impl_->cache_pawn_from_object(object, source);
    }

    auto RuntimePlayerContextService::cache_pawn_from_player_controller(
        UObject* controller, std::wstring_view source, std::wstring_view controller_name_hint,
        bool trust_cached_controller) -> bool
    {
        return impl_->cache_pawn_from_player_controller(controller, source, controller_name_hint,
                                                        trust_cached_controller);
    }

    auto RuntimePlayerContextService::read_object_function_param(UFunction* function_object,
                                                                 void* params,
                                                                 std::wstring_view param_name) const
        -> UObject*
    {
        return impl_->read_object_function_param(function_object, params, param_name);
    }

    auto RuntimePlayerContextService::cache_pawn_from_hook_context(UObject* context,
                                                                   UObject* frame_object,
                                                                   std::wstring_view source) -> bool
    {
        return impl_->cache_pawn_from_hook_context(context, frame_object, source);
    }

    auto RuntimePlayerContextService::write_map_ready_marker(std::wstring_view source,
                                                             UObject* context,
                                                             UObject* frame_object,
                                                             bool promote_trusted_playable_context)
        -> bool
    {
        return impl_->write_map_ready_marker(source, context, frame_object,
                                             promote_trusted_playable_context);
    }

    auto RuntimePlayerContextService::bootstrap_sora_state_from_trusted_pawn(
        UObject* pawn, std::wstring_view source) -> bool
    {
        return impl_->bootstrap_sora_state_from_trusted_pawn(pawn, source);
    }

    auto RuntimePlayerContextService::revalidate_sora_state_from_event(std::wstring_view source,
                                                                       bool log_unchanged) -> bool
    {
        return impl_->revalidate_sora_state_from_event(source, log_unchanged);
    }

    auto RuntimePlayerContextService::process_pending_grant_from_runtime_ready_event(
        std::wstring_view reason, bool refresh_playable_context) -> void
    {
        impl_->process_pending_grant_from_runtime_ready_event(reason, refresh_playable_context);
    }

    auto RuntimePlayerContextService::maybe_clear_special_battle_from_map_root_change(
        std::wstring_view observed_name, std::wstring_view source) -> bool
    {
        return impl_->maybe_clear_special_battle_from_map_root_change(observed_name, source);
    }

    auto RuntimePlayerContextService::maybe_clear_special_battle_from_game_over_recovery(
        std::wstring_view observed_name, std::wstring_view source) -> bool
    {
        return impl_->maybe_clear_special_battle_from_game_over_recovery(observed_name, source);
    }

    auto RuntimePlayerContextService::maybe_clear_special_battle_from_sora_level_owner(
        UObject* level_owner) -> bool
    {
        return impl_->maybe_clear_special_battle_from_sora_level_owner(level_owner);
    }

    auto RuntimePlayerContextService::clear_blocking_ui_from_starting_player(
        std::wstring_view new_player_name) -> void
    {
        impl_->clear_blocking_ui_from_starting_player(new_player_name);
    }

    auto RuntimePlayerContextService::clear_blocking_ui_for_playable_context(
        std::wstring_view reason, std::wstring_view event_name) -> void
    {
        impl_->clear_blocking_ui_for_playable_context(reason, event_name);
    }

    auto RuntimePlayerContextService::activate_blocking_ui_from_sora_game_over_state(
        std::wstring_view state_class, const std::optional<uint8_t>& state_id) -> void
    {
        impl_->activate_blocking_ui_from_sora_game_over_state(state_class, state_id);
    }

    auto
    RuntimePlayerContextService::is_bistro_starting_player(std::wstring_view new_player_name) const
        -> bool
    {
        return impl_->is_bistro_starting_player(new_player_name);
    }

    auto RuntimePlayerContextService::mark_bistro_visited_from_starting_player(
        std::wstring_view new_player_name) -> void
    {
        impl_->mark_bistro_visited_from_starting_player(new_player_name);
    }

    auto
    RuntimePlayerContextService::mark_bistro_return_waiting_for_map_name(std::wstring_view source)
        -> void
    {
        impl_->mark_bistro_return_waiting_for_map_name(source);
    }

    auto
    RuntimePlayerContextService::release_bistro_return_gate_from_map_name(std::wstring_view source)
        -> bool
    {
        return impl_->release_bistro_return_gate_from_map_name(source);
    }

    auto RuntimePlayerContextService::trusted_current_playable_cached_sora_pawn() const -> UObject*
    {
        return impl_->trusted_current_playable_cached_sora_pawn();
    }
} // namespace kh3ap
