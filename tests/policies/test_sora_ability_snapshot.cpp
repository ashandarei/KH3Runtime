#include "kh3ap/features/grants/abilities/sora_ability_snapshot.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    using namespace kh3ap;
    std::array<RuntimeSoraAbilityEntryState, 4> states{};
    std::array<uint32_t, 4> source{0, 1, 3, 2};
    int reads = 0;
    auto reader = [&](uint8_t first, std::span<uint32_t> entries)
    {
        ++reads;
        assert(first == 113);
        assert(entries.size() == source.size());
        std::copy(source.begin(), source.end(), entries.begin());
        return true;
    };

    assert(read_runtime_sora_ability_snapshot(113, states, reader) ==
           RuntimeSoraAbilitySnapshotResult::Read);
    assert(reads == 1);
    assert(!states[0].owned && !states[0].equipped);
    assert(states[1].owned && !states[1].equipped);
    assert(states[2].owned && states[2].equipped);
    assert(!states[3].owned && !states[3].equipped);

    // A later poll must see equip/unequip and receipt changes, including
    // entries with unrelated source/count flags set.
    source = {0x80000003, 0, 0x10001, 0x40003};
    assert(read_runtime_sora_ability_snapshot(113, states, reader) ==
           RuntimeSoraAbilitySnapshotResult::Read);
    assert(reads == 2);
    assert(states[0].owned && states[0].equipped);
    assert(!states[1].owned && !states[1].equipped);
    assert(states[2].owned && !states[2].equipped);
    assert(states[3].owned && states[3].equipped);

    // A failed or partial native read must not expose stale or partial values.
    assert(read_runtime_sora_ability_snapshot(113, states,
                                              [](uint8_t, std::span<uint32_t> entries)
                                              {
                                                  entries[0] = 3;
                                                  return false;
                                              }) == RuntimeSoraAbilitySnapshotResult::Unavailable);
    for (const auto state : states)
    {
        assert(!state.owned && !state.equipped);
    }

    auto unexpected_read = [](uint8_t, std::span<uint32_t>)
    {
        assert(false && "invalid range reached native reader");
        return true;
    };
    assert(read_runtime_sora_ability_snapshot(0, states, unexpected_read) ==
           RuntimeSoraAbilitySnapshotResult::InvalidRange);
    assert(read_runtime_sora_ability_snapshot(253, states, unexpected_read) ==
           RuntimeSoraAbilitySnapshotResult::InvalidRange);
    assert(read_runtime_sora_ability_snapshot(113, {}, unexpected_read) ==
           RuntimeSoraAbilitySnapshotResult::InvalidRange);
    std::array<RuntimeSoraAbilityEntryState, 256> oversized{};
    assert(read_runtime_sora_ability_snapshot(1, oversized, unexpected_read) ==
           RuntimeSoraAbilitySnapshotResult::InvalidRange);

    std::array<RuntimeSoraAbilityEntryState, 1> last{};
    assert(read_runtime_sora_ability_snapshot(255, last,
                                              [](uint8_t first, std::span<uint32_t> entries)
                                              {
                                                  assert(first == 255 && entries.size() == 1);
                                                  entries[0] = std::numeric_limits<uint32_t>::max();
                                                  return true;
                                              }) == RuntimeSoraAbilitySnapshotResult::Read);
    assert(last[0].owned && last[0].equipped);
    // Non-contiguous unlocks share one live read, even when requested out of
    // order. Duplicate IDs preserve caller order without another bank lookup.
    constexpr std::array<uint8_t, 4> selected{134, 115, 134, 113};
    std::array<RuntimeSoraAbilityEntryState, selected.size()> selection{};
    int range_reads = 0;
    const auto range_reader = [&](uint8_t first, std::span<RuntimeSoraAbilityEntryState> range)
    {
        ++range_reads;
        assert(first == 113 && range.size() == 22);
        range[0] = {true, false};
        range[115 - first] = {true, true};
        range[134 - first] = {false, false};
        return true;
    };
    assert(read_runtime_sora_ability_selection(selected, selection, range_reader) ==
           RuntimeSoraAbilitySnapshotResult::Read);
    assert(range_reads == 1);
    assert(!selection[0].owned && !selection[0].equipped);
    assert(selection[1].owned && selection[1].equipped);
    assert(!selection[2].owned && !selection[2].equipped);
    assert(selection[3].owned && !selection[3].equipped);
    // New events read live values again; no bank or ownership state is cached.
    assert(read_runtime_sora_ability_selection(
               selected, selection,
               [&](uint8_t first, std::span<RuntimeSoraAbilityEntryState> range)
               {
                   ++range_reads;
                   assert(first == 113 && range.size() == 22);
                   range[134 - first] = {true, true};
                   return true;
               }) == RuntimeSoraAbilitySnapshotResult::Read);
    assert(range_reads == 2 && selection[0].owned && selection[2].equipped);
    // Discard a partially filled range on failure, including preceding results.
    assert(read_runtime_sora_ability_selection(
               selected, selection,
               [](uint8_t, std::span<RuntimeSoraAbilityEntryState> range)
               {
                   range[0] = {true, true};
                   return false;
               }) == RuntimeSoraAbilitySnapshotResult::Unavailable);
    for (const auto entry : selection)
    {
        assert(!entry.owned && !entry.equipped);
    }
    const auto unexpected_range = [](uint8_t, std::span<RuntimeSoraAbilityEntryState>)
    {
        assert(false && "invalid selection reached bank reader");
        return false;
    };
    assert(read_runtime_sora_ability_selection({}, selection, unexpected_range) ==
           RuntimeSoraAbilitySnapshotResult::InvalidRange);
    assert(read_runtime_sora_ability_selection(selected, last, unexpected_range) ==
           RuntimeSoraAbilitySnapshotResult::InvalidRange);
    constexpr std::array<uint8_t, 1> invalid_selection{0};
    assert(read_runtime_sora_ability_selection(invalid_selection, last, unexpected_range) ==
           RuntimeSoraAbilitySnapshotResult::InvalidRange);
    constexpr std::array<uint8_t, 2> extremes{255, 1};
    std::array<RuntimeSoraAbilityEntryState, 2> boundary{};
    assert(read_runtime_sora_ability_selection(
               extremes, boundary,
               [](uint8_t first, std::span<RuntimeSoraAbilityEntryState> range)
               {
                   assert(first == 1 && range.size() == 255);
                   range.front() = {true, false};
                   range.back() = {true, true};
                   return true;
               }) == RuntimeSoraAbilitySnapshotResult::Read);
    assert(boundary[0].equipped && boundary[1].owned && !boundary[1].equipped);
    std::array<uint8_t, 257> too_many{};
    too_many.fill(1);
    std::array<RuntimeSoraAbilityEntryState, 257> too_many_states{};
    assert(read_runtime_sora_ability_selection(too_many, too_many_states, unexpected_range) ==
           RuntimeSoraAbilitySnapshotResult::InvalidRange);
    return 0;
}
