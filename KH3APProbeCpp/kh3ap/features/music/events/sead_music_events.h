#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kh3ap
{
    enum class SeadMusicEvent : size_t
    {
        Loaded, Set, SetPath, Play, Stop, Suspend, AutoPlay, AutoResume, Reset, Transfer, Finished, ConfigCategoryVolume, ConfigVolumeAll, EquipmentRebuilt, Count
    };
    inline constexpr size_t sead_music_event_count = static_cast<size_t>(SeadMusicEvent::Count);
    inline constexpr std::array<std::string_view, sead_music_event_count> sead_music_event_names{
        "loaded", "set", "set_path", "play", "stop", "suspend", "auto_play", "auto_resume", "reset", "transfer", "finished", "config_category_volume", "config_volume_all", "equipment_rebuilt"};
    constexpr auto sead_music_event_bit(SeadMusicEvent event) -> uint32_t
    {
        return uint32_t{1} << static_cast<size_t>(event);
    }

    // Producers only publish a reason. No UObject survives a callback and no
    // game/asset work runs on an audio/loader thread or reentrantly inside SEAD.
    struct SeadMusicMailbox
    {
        std::atomic<uint32_t> pending{};
        auto post(SeadMusicEvent event) noexcept -> void
        { pending.fetch_or(sead_music_event_bit(event), std::memory_order_release); }
        auto take() noexcept -> uint32_t
        { return pending.exchange(0, std::memory_order_acquire); }
    };
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);

    // Finish is an internal branch, not a function entry. The stub only sets a
    // mailbox bit (and optional diagnostic counter), preserving RAX and flags.
    // Its final indirect jump contains an address filled before the game thread
    // returns from hook installation. The trampoline executes the original
    // state store and jump; it must not relocate the following basic block.
    inline auto build_sead_finish_stub(uint64_t mailbox, uint64_t counter) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> code{0x9C, 0x50}; // pushfq; push rax
        const auto address = [&](uint64_t value) {
            for (unsigned i = 0; i < 8; ++i) code.push_back(static_cast<uint8_t>(value >> (8 * i)));
        };
        code.insert(code.end(), {0x48, 0xB8}); address(mailbox);
        code.insert(code.end(), {0xF0, 0x81, 0x08}); // lock or dword ptr [rax], mask
        const uint32_t bit = sead_music_event_bit(SeadMusicEvent::Finished);
        for (unsigned i = 0; i < 4; ++i) code.push_back(static_cast<uint8_t>(bit >> (8 * i)));
        if (counter)
        {
            code.insert(code.end(), {0x48, 0xB8}); address(counter);
            code.insert(code.end(), {0xF0, 0x48, 0xFF, 0x00}); // lock inc qword ptr [rax]
        }
        code.insert(code.end(), {0x58, 0x9D, 0xFF, 0x25, 0, 0, 0, 0}); // pop rax; popfq; jmp [rip]
        address(0); // trampoline destination
        return code;
    }

    auto install_sead_music_events() -> bool; // once, on the game thread
    auto uninstall_sead_music_events() -> void;
    auto take_sead_music_events() noexcept -> uint32_t;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    struct SeadMusicEventSnapshot
    {
        bool installed{};
        uint32_t game_thread{};
        uint64_t other_thread_calls{}, suppressed_calls{};
        std::array<uint64_t, sead_music_event_count> calls{};
    };
    auto sead_music_event_snapshot() -> SeadMusicEventSnapshot;
#endif
}
