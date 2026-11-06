#include "kh3ap/features/world/portals/portal_spawn_helpers.h"

#include <cassert>

int main()
{
    static_assert(kh3ap::runtime_portal_max_class_load_requests == 3);
    static_assert(kh3ap::runtime_portal_class_load_request_allowed(0));
    static_assert(kh3ap::runtime_portal_class_load_request_allowed(2));
    static_assert(!kh3ap::runtime_portal_class_load_request_allowed(3));

    // Distinct elements of one backing object keep these fake pawn identities
    // stable across all context calls in optimized test builds.
    int pawn_identities[2]{};
    auto& playable_pawn_identity = pawn_identities[0];
    auto& stale_pawn_identity = pawn_identities[1];
    constexpr auto map_path = L"/Game/Levels/bx/bx_02/bx_02";
    constexpr auto pawn_name =
            L"p_bx001_Pawn_C /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel.p_bx001_Pawn_C_0 @123 real=true";
    constexpr auto map_ready_key =
            L"World /Game/Levels/bx/bx_02/bx_02.bx_02|p_bx001_Pawn_C "
            L"/Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel.p_bx001_Pawn_C_0 @123 real=true";

    const auto playable = kh3ap::resolve_runtime_portal_auto_spawn_context(
            L"RuntimePlayablePawnContext",
            L"World /Game/Levels/bx/bx_02/bx_02.bx_02",
            L"Package /Game/Levels/bx/bx_02/bx_02",
            pawn_name,
            &playable_pawn_identity,
            pawn_name,
            &playable_pawn_identity,
            map_path,
            L"",
            L"");
    assert(playable.can_progress());
    assert(!playable.used_pause_menu_fallback);

    constexpr auto early_cached_pawn_name =
            L"p_bx001_Pawn_C /Game/Levels/bx/bx_02/bx_02.bx_02:PersistentLevel.p_bx001_Pawn_C_0";
    const auto playable_after_diagnostic_name_refresh = kh3ap::resolve_runtime_portal_auto_spawn_context(
            L"RuntimePlayablePawnContext",
            L"World /Game/Levels/bx/bx_02/bx_02.bx_02",
            L"Package /Game/Levels/bx/bx_02/bx_02",
            pawn_name,
            &playable_pawn_identity,
            early_cached_pawn_name,
            &playable_pawn_identity,
            map_path,
            map_ready_key,
            L"");
    assert(playable_after_diagnostic_name_refresh.can_progress());
    assert(playable_after_diagnostic_name_refresh.context_key == map_ready_key);

    const auto different_pawn_with_same_diagnostic_name = kh3ap::resolve_runtime_portal_auto_spawn_context(
            L"RuntimePlayablePawnContext",
            L"World /Game/Levels/bx/bx_02/bx_02.bx_02",
            L"Package /Game/Levels/bx/bx_02/bx_02",
            pawn_name,
            &stale_pawn_identity,
            pawn_name,
            &playable_pawn_identity,
            map_path,
            map_ready_key,
            L"");
    assert(different_pawn_with_same_diagnostic_name.target_map_is_current());
    assert(!different_pawn_with_same_diagnostic_name.can_progress());

    const auto pause_menu_camera = kh3ap::resolve_runtime_portal_auto_spawn_context(
            L"TresSetPauseMenuType",
            L"World /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s.bx_02_camera_ms040s",
            L"Package /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s",
            L"",
            &playable_pawn_identity,
            pawn_name,
            &playable_pawn_identity,
            map_path,
            map_ready_key,
            L"");
    assert(pause_menu_camera.can_progress());
    assert(pause_menu_camera.used_pause_menu_fallback);
    assert(!pause_menu_camera.used_prior_playable_context);
    assert(pause_menu_camera.context_key == map_ready_key);

    const auto pause_menu_after_playable = kh3ap::resolve_runtime_portal_auto_spawn_context(
            L"TresSetPauseMenuType",
            L"World /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s.bx_02_camera_ms040s",
            L"Package /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s",
            L"",
            &playable_pawn_identity,
            pawn_name,
            &playable_pawn_identity,
            map_path,
            L"",
            playable.context_key);
    assert(pause_menu_after_playable.can_progress());
    assert(pause_menu_after_playable.used_pause_menu_fallback);
    assert(pause_menu_after_playable.used_prior_playable_context);
    assert(pause_menu_after_playable.context_key == playable.context_key);

    const auto missing_map_ready = kh3ap::resolve_runtime_portal_auto_spawn_context(
            L"TresSetPauseMenuType",
            L"World /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s.bx_02_camera_ms040s",
            L"Package /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s",
            L"",
            &playable_pawn_identity,
            pawn_name,
            &playable_pawn_identity,
            map_path,
            L"",
            L"");
    assert(missing_map_ready.target_map_is_current());
    assert(!missing_map_ready.can_progress());

    const auto stale_prior_playable = kh3ap::resolve_runtime_portal_auto_spawn_context(
            L"TresSetPauseMenuType",
            L"World /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s.bx_02_camera_ms040s",
            L"Package /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s",
            L"",
            &playable_pawn_identity,
            pawn_name,
            &playable_pawn_identity,
            map_path,
            L"",
            L"World /Game/Levels/bx/bx_03/bx_03.bx_03|p_bx001_Pawn_C "
            L"/Game/Levels/bx/bx_03/bx_03.bx_03:PersistentLevel.p_bx001_Pawn_C_0 @456 real=true");
    assert(stale_prior_playable.target_map_is_current());
    assert(!stale_prior_playable.can_progress());

    const auto stale_pawn = kh3ap::resolve_runtime_portal_auto_spawn_context(
            L"TresSetPauseMenuType",
            L"World /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s.bx_02_camera_ms040s",
            L"Package /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s",
            L"",
            &playable_pawn_identity,
            L"p_bx001_Pawn_C /Game/Levels/bx/bx_03/bx_03.bx_03:PersistentLevel.p_bx001_Pawn_C_0",
            &stale_pawn_identity,
            map_path,
            map_ready_key,
            playable.context_key);
    assert(!stale_pawn.target_map_is_current());
    assert(!stale_pawn.can_progress());

    const auto unrelated_hook = kh3ap::resolve_runtime_portal_auto_spawn_context(
            L"TresMapJump",
            L"World /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s.bx_02_camera_ms040s",
            L"Package /Game/Levels/bx/bx_02/umap/bx_02_camera_ms040s",
            L"",
            &playable_pawn_identity,
            pawn_name,
            &playable_pawn_identity,
            map_path,
            map_ready_key,
            playable.context_key);
    assert(unrelated_hook.target_map_is_current());
    assert(!unrelated_hook.can_progress());
    return 0;
}
