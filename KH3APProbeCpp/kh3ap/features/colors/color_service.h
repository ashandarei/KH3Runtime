#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    // How one character's material parameters relate to each other.
    // Selected by the color_randomization setting; the two modes are
    // deliberately different in kind, not just in degree.
    enum class RuntimeColorHarmonyMode
    {
        // One base hue per character. The four tint zones are variations of
        // it and the hair set is internally coherent, so each character
        // reads as a deliberate outfit. Characters still differ from each
        // other - the harmony is within a character, not across the party.
        Coordinated,
        // Every parameter drawn independently. Zones clash with each other
        // and hair base/tips/subsurface are unrelated, which is the point.
        Wild,
    };

    struct RuntimeColorServiceConfig
    {
        RuntimeState& runtime_state;
        // Supplies the generated seed identity. Colours are derived from
        // it rather than stored, so the same seed always yields the same
        // characters. Empty (no seed metadata) falls back to a fixed
        // identity so colours stay stable rather than varying per launch.
        std::function<std::wstring()> seed_identity_resolver{};
        // Selects the harmony mode. Unset means Coordinated.
        std::function<RuntimeColorHarmonyMode()> harmony_mode_resolver{};
        // Whether the feature runs at all, from the color_randomization
        // setting - seeded by the yaml option, overridden by the in-game AP
        // Options row. Unset means enabled, so nothing that constructs this
        // service without an opinion silently loses its colours.
        std::function<bool()> enabled_resolver{};
    };

    // Native replacement for the retired Blueprint/map/anim-notify color
    // randomization mechanism. Fully decoupled from the party and grant
    // features it borrows signals/safety gating from: they call into this
    // service, it never reaches back into their internals beyond the shared
    // RuntimeState accessors already used for that purpose elsewhere.
    class RuntimeColorService
    {
    public:
        explicit RuntimeColorService(RuntimeColorServiceConfig config);
        ~RuntimeColorService();

        RuntimeColorService(const RuntimeColorService&) = delete;
        auto operator=(const RuntimeColorService&) -> RuntimeColorService& = delete;
        RuntimeColorService(RuntimeColorService&&) noexcept;
        auto operator=(RuntimeColorService&&) noexcept -> RuntimeColorService&;

        auto install_once() -> void;

        // Drives the pending-pawn retry queue: safety gating, and applying
        // the derived colours once a pending pawn is safe to touch. Also
        // notices a change to the color_randomization setting and re-applies
        // in place, so switching it does not wait for the next map load.
        //
        // Equipment rebuilds request weapon coloring; only an unfinished mesh
        // retries. Each weapon palette is derived from its class and seed.
        auto tick(std::chrono::steady_clock::time_point now) -> void;

        // Called from RuntimePartyMemberService's Controller:Possess
        // observation (any pawn, not just party members) and from wherever
        // Sora's own possession is observed. Pawns whose class isn't one of
        // the supported CharacterKeys are ignored.
        auto on_pawn_possessed(RC::Unreal::UObject* pawn) -> void;
        auto on_sora_state(RC::Unreal::UObject* pawn) -> void;

        // Called from the TresMapJump pre-hook, alongside the other
        // lifecycle resets. Every pending/colored pawn belongs to the world
        // being left.
        auto on_map_jump() -> void;

        // Requests a game-thread reroll, safe from the UE4SS input thread.
        // Re-derives every colour from a bumped seed and re-applies it to
        // whatever is currently in the world, without a map change or a
        // restart. Exists for iterating on the colour work: judging a
        // palette change otherwise costs a relaunch and a walk back to the
        // character being looked at.
        auto reroll_colors() -> void;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        [[nodiscard]] auto test_gummi_snapshot() const -> std::string;
#endif

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
