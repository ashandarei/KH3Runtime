#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeBgmShuffleConfig
    {
        // The live BgmShuffleMode, as the AP Options menu last set it.
        std::function<int32_t()> mode;
        // Seeds the draw, so a seed always hears the same shuffle.
        std::function<std::wstring()> seed_identity;
        std::function<bool()> gameplay_ready;
        // Form themes (form_theme.h), from the AP Options menu.
        std::function<bool()> form_themes_enabled;
        std::function<RC::Unreal::UObject*()> sora_pawn;
        // A menu is open (the pause menu included), for custom music to
        // dip as the game's own does.
        std::function<bool()> menu_open{};
        // Only gates the automatic Chaos pool; explicit replacements stay active.
        std::function<bool()> include_custom_music{};
    };

    // Shuffles field and battle music. The table comes from the mode and the
    // seed identity (bgm_shuffle_table). On every level load the
    // service rewrites the WorldSettings and world load asset references,
    // then points the Field and Battle slots at the replacements before they
    // start; a slot found playing an original later is switched on the fly.
    // A mode change applies to the current level at once, and going back to
    // Vanilla restores what the service had rewritten. Native audio, context,
    // settings, UI and form events drive reconciliation; idle frames do no scans.
    class RuntimeBgmShuffleService
    {
    public:
        explicit RuntimeBgmShuffleService(RuntimeBgmShuffleConfig config);
        ~RuntimeBgmShuffleService();

        RuntimeBgmShuffleService(const RuntimeBgmShuffleService&) = delete;
        auto operator=(const RuntimeBgmShuffleService&) -> RuntimeBgmShuffleService& = delete;

        // Game thread only.
        auto tick() -> void;
        // Cached on music events, including Vanilla and custom decode completion.
        [[nodiscard]] auto now_playing_title() const -> const std::wstring&;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        [[nodiscard]] auto test_snapshot() -> std::string;
#endif

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
