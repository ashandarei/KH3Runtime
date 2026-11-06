#pragma once

#include "kh3ap/features/music/audio/custom_music_library.h"

#include <cstddef>
#include <filesystem>
#include <memory>

namespace kh3ap
{
    // Plays user-supplied music files outside the game's audio engine,
    // through XAudio2, each looping until stopped. Like the game's own music
    // they keep playing in menus and in the background. Custom tracks live as
    // plain files in the player's selected folder (mod-local custom_music/
    // by default) and are never cooked
    // into a pak. RIFF WAV (PCM or float) and Ogg Vorbis are read.
    //
    // One voice per channel, so a field track can hold its place while a
    // battle track plays, as SEAD does for the game's music.
    class RuntimeCustomMusicPlayer
    {
    public:
        // One per SEAD slot, with one spare channel.
        static constexpr size_t channel_count = 10;

        RuntimeCustomMusicPlayer();
        ~RuntimeCustomMusicPlayer();

        RuntimeCustomMusicPlayer(const RuntimeCustomMusicPlayer&) = delete;
        auto operator=(const RuntimeCustomMusicPlayer&) -> RuntimeCustomMusicPlayer& = delete;

        // Plays a track on a channel, looping between its loop points. The
        // same file resumes where it was held; another file is loaded on a
        // worker thread and publishes a completion event to the music service.
        // The next event drain starts it from the top. False until it plays; a file that cannot
        // be read or played is not retried before stop_all().
        auto play(size_t channel, const CustomMusicTrack& track) -> bool;
        // Keeps the channel's file and position for a later play().
        auto hold(size_t channel) -> void;
        auto stop(size_t channel) -> void;
        auto stop_all() -> void;
        // The channel's file, playing or held; empty when stopped.
        [[nodiscard]] auto file(size_t channel) const -> const std::filesystem::path&;
        // Includes held voices and pending decodes, which still need cleanup
        // when the last custom mapping is removed.
        [[nodiscard]] auto active() const -> bool;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        [[nodiscard]] auto test_snapshot() const -> std::string;
#endif
        // Linear gain for every channel, 0 silent to 1 as recorded, times
        // each track's own volume; reached through a fade.
        auto set_volume(float volume) -> void;
        // Moves the fades on: tracks fade in when they start or resume and
        // out when held or stopped, as the game's music does. Called every
        // frame.
        auto update() -> void;

        // The game's own music fades over the same time (slot_mute_fade).
        static constexpr float fade_seconds = 0.5f;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // Mods/KH3APProbeCpp/custom_music, next to the mod's dlls folder.
    auto custom_music_directory() -> std::filesystem::path;
}
