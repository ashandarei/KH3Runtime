#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/story/save_points/save_point_spawn_helpers.h"

#include <cassert>
#include <chrono>

int main()
{
    using kh3ap::RuntimePlayerStartSelectionDecision;

    static_assert(kh3ap::runtime_he02_save_point_needed(0));
    static_assert(kh3ap::runtime_he02_save_point_needed(299));
    static_assert(!kh3ap::runtime_he02_save_point_needed(300));
    static_assert(!kh3ap::runtime_he02_save_point_needed(301));
    static_assert(!kh3ap::runtime_he02_save_point_needed(2101));

    kh3ap::RuntimeAutoSavePointSpawnState state{};
    state.begin_context(L"/Game/Levels/fz/fz_02");

    assert(!state.already_spawned_for(state.last_context_key));
    assert(!state.finished_for(state.last_context_key));

    state.spawn_attempted = true;
    assert(state.already_spawned_for(L"/Game/Levels/fz/fz_02"));
    assert(state.finished_for(L"/Game/Levels/fz/fz_02"));

    // A diagnostic-name refresh or pawn replacement within the same map visit
    // must not rearm an already successful actor spawn.
    state.begin_context(L"/Game/Levels/fz/fz_02");
    assert(state.already_spawned_for(L"/Game/Levels/fz/fz_02"));

    state.begin_context(L"/Game/Levels/he/he_02");
    assert(!state.already_spawned_for(L"/Game/Levels/he/he_02"));
    assert(!state.finished_for(L"/Game/Levels/he/he_02"));
    assert(state.attempt_count == 0);
    assert(!state.asset_load_requested);

    state.terminal_failure = true;
    assert(state.finished_for(L"/Game/Levels/he/he_02"));
    state.reset();
    assert(state.last_context_key.empty());
    assert(!state.finished_for(L"/Game/Levels/he/he_02"));

    assert(kh3ap::should_retry_runtime_save_point_auto_spawn(false, true, true, 1));
    assert(!kh3ap::should_retry_runtime_save_point_auto_spawn(false, true, false, 2));
    assert(kh3ap::should_retry_runtime_save_point_auto_spawn(false, false, false, 1));
    assert(!kh3ap::should_retry_runtime_save_point_auto_spawn(false, false, false, 2));
    assert(!kh3ap::should_retry_runtime_save_point_auto_spawn(true, false, false, 1));

    kh3ap::RuntimeGrantPawnState pawn_state{};
    auto* const pawn = reinterpret_cast<RC::Unreal::UObject*>(0x1234);
    const auto first_seen_at = std::chrono::steady_clock::now();
    pawn_state.update_candidate(pawn, L"Sora@visit-one", first_seen_at);
    assert(pawn_state.candidate_trusted());
    assert(pawn_state.object() == pawn);
    assert(pawn_state.name() == L"Sora@visit-one");
    assert(pawn_state.stable_for(first_seen_at + std::chrono::seconds{5}) == std::chrono::seconds{5});

    auto* live_controller = reinterpret_cast<RC::Unreal::UObject*>(0x2468);
    pawn_state.update_controller([&live_controller]() { return live_controller; }, L"SoraController");
    assert(pawn_state.controller_object() == live_controller);

    pawn_state.update_candidate(
            pawn,
            L"Sora@visit-one real=true",
            first_seen_at + std::chrono::seconds{1});
    assert(pawn_state.candidate_trusted());
    assert(pawn_state.object() == pawn);
    assert(pawn_state.name() == L"Sora@visit-one real=true");
    assert(pawn_state.first_seen_at() == first_seen_at);
    assert(pawn_state.stable_for(first_seen_at + std::chrono::seconds{5}) == std::chrono::seconds{5});
    assert(pawn_state.controller_object() == live_controller);

    // The engine weak resolver can expire while the old address remains
    // readable/reused. The state must resolve again instead of retaining it.
    live_controller = nullptr;
    assert(pawn_state.controller_object() == nullptr);
    live_controller = reinterpret_cast<RC::Unreal::UObject*>(0x2468);
    assert(pawn_state.controller_object() == live_controller);

    constexpr std::wstring_view published_context_name =
            L"p_ex001_Pawn_C /Game/Levels/fz/fz_02/fz_02.fz_02:PersistentLevel.p_ex001_Pawn_C_0 "
            L"@2116745637568 real=true";
    constexpr std::wstring_view refreshed_diagnostic_name =
            L"p_ex001_Pawn_C /Game/Levels/fz/fz_02/fz_02.fz_02:PersistentLevel.p_ex001_Pawn_C_0";
    assert(published_context_name != refreshed_diagnostic_name);
    assert(kh3ap::runtime_save_point_playable_context_matches_pawn(
            L"RuntimePlayablePawnContext",
            pawn,
            pawn));

    auto* const stale_context_pawn = reinterpret_cast<RC::Unreal::UObject*>(0x4321);
    assert(!kh3ap::runtime_save_point_playable_context_matches_pawn(
            L"RuntimePlayablePawnContext",
            stale_context_pawn,
            pawn));
    assert(!kh3ap::runtime_save_point_playable_context_matches_pawn(
            L"TresSetPauseMenuType",
            pawn,
            pawn));

    pawn_state.update_candidate(
            pawn,
            L"",
            first_seen_at + std::chrono::seconds{2});
    assert(pawn_state.candidate_trusted());
    assert(pawn_state.object() == pawn);
    assert(pawn_state.name().empty());
    assert(pawn_state.first_seen_at() == first_seen_at);

    auto* const replacement_pawn = reinterpret_cast<RC::Unreal::UObject*>(0x5678);
    const auto replacement_seen_at = first_seen_at + std::chrono::seconds{6};
    pawn_state.update_candidate(replacement_pawn, L"Sora@visit-two", replacement_seen_at);
    assert(pawn_state.controller_object() == nullptr);
    assert(pawn_state.controller_name().empty());
    assert(pawn_state.candidate_trusted());
    assert(pawn_state.object() == replacement_pawn);
    assert(pawn_state.first_seen_at() == replacement_seen_at);
    assert(pawn_state.stable_for(replacement_seen_at) == std::chrono::steady_clock::duration{});

    pawn_state.clear_candidate();
    assert(!pawn_state.candidate_trusted());
    assert(pawn_state.object() == nullptr);

    auto* live_pawn = pawn;
    pawn_state.update_candidate(pawn, L"CinematicSora", first_seen_at,
            [&live_pawn]() { return live_pawn; });
    live_pawn = nullptr;
    assert(pawn_state.object() == nullptr);
    // A new UObject at the same address must start a fresh stability interval.
    pawn_state.update_candidate(pawn, L"PlayableSora", replacement_seen_at,
            [pawn]() { return pawn; });
    assert(pawn_state.object() == pawn);
    assert(pawn_state.first_seen_at() == replacement_seen_at);

    pawn_state.update_controller([&live_controller]() { return live_controller; }, L"NextController");
    assert(pawn_state.controller_object() == live_controller);
    pawn_state.reset();
    assert(pawn_state.controller_object() == nullptr);
    assert(pawn_state.controller_name().empty());

    constexpr std::wstring_view requested_start = L"fz_02_Lv_AP_Save_01";
    assert(kh3ap::decide_runtime_player_start_selection(
                   L"fz_02_Lv_AutoSave_01",
                   requested_start,
                   true,
                   true,
                   false) == RuntimePlayerStartSelectionDecision::Ignore);
    assert(kh3ap::decide_runtime_player_start_selection(
                   requested_start,
                   requested_start,
                   false,
                   true,
                   false) == RuntimePlayerStartSelectionDecision::Ignore);
    assert(kh3ap::decide_runtime_player_start_selection(
                   requested_start,
                   requested_start,
                   true,
                   false,
                   false) == RuntimePlayerStartSelectionDecision::RequestedActorUnavailable);
    assert(kh3ap::decide_runtime_player_start_selection(
                   requested_start,
                   requested_start,
                   true,
                   true,
                   true) == RuntimePlayerStartSelectionDecision::KeepSelected);
    assert(kh3ap::decide_runtime_player_start_selection(
                   requested_start,
                   requested_start,
                   true,
                   true,
                   false) == RuntimePlayerStartSelectionDecision::OverrideSelected);
    return 0;
}
