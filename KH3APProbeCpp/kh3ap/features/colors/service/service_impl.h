#pragma once

#include "kh3ap/core/settings/color_randomization_mode.h"
#include "kh3ap/features/colors/color_service.h"
#include "kh3ap/features/colors/color_apply_state.h"
#include "kh3ap/features/colors/color_gummi.h"
#include "kh3ap/features/colors/color_timing.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap
{
    struct RuntimeColorService::Impl
    {
        explicit Impl(RuntimeColorServiceConfig config_in) : config(config_in)
        {
        }

        RuntimeColorServiceConfig config;
        RuntimeColorApplyState state{};

        // Cosmetics use their own short debounce; inventory grants retain
        // their longer stability window. AnimInstance/render readiness below
        // still decides whether a newly spawned mesh can be painted.
        // A companion pawn is only just spawned when detected, so its mesh
        // may not be set up yet. The previous fix for that was a blind
        // 5-second wait, which was tested live and did NOT prevent the crash
        // (the cause turned out to be the Blueprint's save lookup, now gone
        // entirely). Blind waiting is therefore replaced by an observable
        // signal: a mesh with a live AnimInstance is fully set up and
        // ticking - the same guarantee the retired anim-notify trigger had
        // by construction, since it fired from that very component.
        //
        // Until this grace expires only a mesh with an AnimInstance is
        // accepted; after it, any usable mesh is, so a character that
        // legitimately never has one still gets coloured instead of being
        // blocked forever.
        static constexpr std::chrono::milliseconds anim_instance_grace{3000};
        std::wstring last_world_name_{};
        std::chrono::steady_clock::time_point world_first_seen_at_{};
        // Pawns whose one-off full mesh candidate dump has been emitted.
        // Cleared on map jump with the rest of the per-world state.
        std::vector<RC::Unreal::FWeakObjectPtr> mesh_dump_logged_{};

        // try_apply now returns false (rather than crashing or
        // succeeding) for as long as a pawn has no render-ready mesh, and
        // the pending queue is walked every frame. Without a floor on the
        // retry rate a companion that never settles would re-scan its
        // components and emit the surrounding trace ~60 times a second
        // for the rest of the session.
        static constexpr auto apply_retry_interval = color_apply_retry_interval;
        std::vector<std::pair<RC::Unreal::FWeakObjectPtr, std::chrono::steady_clock::time_point>>
            last_attempt_at_{};

        // Colours are derived from this. With no seed metadata (no bridge,
        // or a plain unrandomised playthrough) a fixed identity keeps them
        // stable across launches instead of varying per session.
        // Fails open: an unset resolver, or one that cannot reach the
        // bridge yet, leaves the feature on. Losing colours silently
        // because slot_data was not readable at that moment would be
        // indistinguishable from the feature being broken.
        [[nodiscard]] auto enabled() const -> bool;

        [[nodiscard]] auto harmony_mode() const -> RuntimeColorHarmonyMode;

        // Bumped by reroll_colors. Folded into the seed so a re-roll is a
        // different draw rather than the same one re-applied, while a
        // re-roll count of zero still reproduces the seed's own colours
        // exactly.
        std::atomic_bool reroll_requested_{false};
        auto apply_reroll(std::chrono::steady_clock::time_point now) -> void;

        uint32_t reroll_counter_{0};
        std::chrono::steady_clock::time_point last_reroll_at_{};
        // Empty until the first tick, so the opening read is not treated
        // as a change and does not force a redundant re-apply.
        std::optional<ColorRandomizationMode> last_mode_{};

        [[nodiscard]] auto seed_identity() const -> std::wstring;

        [[nodiscard]] auto ready_to_attempt(RC::Unreal::UObject* pawn,
                                            std::chrono::steady_clock::time_point now) -> bool;

        [[nodiscard]] auto take_mesh_dump_slot(RC::Unreal::UObject* pawn) -> bool;

        [[nodiscard]] auto world_is_allowed(RC::Unreal::UObject* pawn,
                                            std::chrono::steady_clock::time_point now) -> bool;

        // Class names already reported by note_unsupported_pawn. Kept for the
        // whole process, not reset on map jump: the point is one line per
        // class ever seen, not one per world.
        std::vector<std::wstring> unsupported_logged_{};

        // The last safety reason reported while pawns were waiting, so the
        // line below prints once per distinct reason rather than per tick.
        std::wstring last_blocked_reason_{};

        // Names a pawn that reached detection but matched neither the Sora
        // catalog nor companion_character_keys.
        //
        // Without this the answer to "is this character coloured?" is
        // unobservable: resolve_supported_character_key returns nullopt in
        // silence, so an unknown pawn looks exactly like a pawn that was
        // never detected at all.
        //
        // Debug-only, and deduplicated by class name on top of that, so even
        // with logging on it is one line per class for the session.
        auto note_unsupported_pawn(RC::Unreal::UObject* pawn) -> void;

        auto maybe_queue(RC::Unreal::UObject* pawn, std::chrono::steady_clock::time_point now)
            -> void;

        uint64_t pawn_generation_{~uint64_t{0}};
        bool pawn_discovery_pending_{true};
        std::chrono::steady_clock::time_point pawn_retry_at_{};
        std::chrono::steady_clock::time_point alternate_context_retry_at_{};
        bool weapons_dirty_{true};
        std::chrono::steady_clock::time_point weapon_retry_at_{};
        uint64_t equipment_generation_{};

        // Sora's own Outer. The enemy remap service keys on the same fact
        // (register_level_entity_wipe_out_groups checks the name), so this
        // is established behaviour of this game rather than an assumption
        // about UE in general.
        [[nodiscard]] static auto resolve_persistent_level(RC::Unreal::UObject* reference_pawn)
            -> RC::Unreal::UObject*;

        auto discover_pawns(RC::Unreal::UObject* reference_pawn,
                            std::chrono::steady_clock::time_point now) -> void;

        // Puts every pawn coloured since the last reset back to the
        // materials it shipped with. Switching the feature off otherwise
        // only stops the NEXT character being painted, which reads as the
        // setting not working at all on the party standing in front of you.
        //
        // Same mechanism the skipped slots use, applied to every slot of
        // every tracked pawn rather than one.
        auto restore_all_colored(std::chrono::steady_clock::time_point now) -> void;

        // Everything a re-apply needs, minus the seed bump. reroll_colors
        // adds the bump on top; a settings change wants the same colours
        // redrawn under the new mode, not different ones.
        auto reapply_in_place(std::chrono::steady_clock::time_point now) -> void;

        // Recompose the two resolvers into one mode for the change check.
        [[nodiscard]] auto current_mode() const -> ColorRandomizationMode;

        auto try_apply(const RuntimeColorPendingPawn& entry,
                       std::chrono::steady_clock::time_point now) -> bool;

        // The weapon sweep's schedule. The sweep itself lives in
        // runtime_color_weapons; what belongs here is how often it runs,
        // which is a lifecycle question like the rest of this struct.
        //
        RuntimeColorGummi gummi_{};

        // Native Sora state-write batches request this lookup. Creation events
        // also discover a Link if m_pObj is assigned after its state entry.
        auto observe_active_link(RC::Unreal::UObject* sora,
                                 std::chrono::steady_clock::time_point now) -> void;

        // A Link fades in over its first second or so, and paint written
        // during that fade did not show: live, Ralph and Meow Wow were
        // coloured the moment they spawned and came out looking vanilla.
        // So a Link waits before its first paint, and is painted again a
        // few times after it, in case the appearance rewrites its
        // materials once more.
        static constexpr std::chrono::milliseconds link_settle{1500};
        static constexpr std::array<std::chrono::milliseconds, 3> link_repaint_after{
            std::chrono::milliseconds{2000}, std::chrono::milliseconds{3000},
            std::chrono::milliseconds{5000}};
        struct LinkRepaint
        {
            RC::Unreal::FWeakObjectPtr pawn{};
            std::wstring key{};
            std::chrono::steady_clock::time_point due{};
            size_t step{};
        };
        std::vector<LinkRepaint> link_repaints_{};

        auto repaint_links(std::chrono::steady_clock::time_point now) -> void;

        auto color_equipped_weapons(std::chrono::steady_clock::time_point now) -> void;
    };
} // namespace kh3ap
