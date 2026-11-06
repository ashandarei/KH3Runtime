#include "kh3ap/features/story/minigames/olympus_coliseum_completion.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <string_view>

namespace
{
    auto append_int32(std::string& bytes, int32_t value) -> void
    {
        const size_t offset = bytes.size();
        bytes.resize(offset + sizeof(value));
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    auto append_fstring(std::string& bytes, std::string_view value) -> void
    {
        append_int32(bytes, static_cast<int32_t>(value.size() + 1));
        bytes.append(value);
        bytes.push_back('\0');
    }

    auto make_progress_entry(
            std::string_view cup_entry,
            bool difficulty_beaten) -> std::string
    {
        std::string bytes{"GVAS-test-prefix"};
        append_fstring(bytes, cup_entry);
        append_fstring(
                bytes,
                kh3ap::runtime_olympus_coliseum_difficulty_beaten_property);
        append_fstring(bytes, "BoolProperty");
        append_int32(bytes, 0);
        append_int32(bytes, 0);
        bytes.push_back(difficulty_beaten ? '\1' : '\0');
        // No property GUID follows this tag.
        bytes.push_back('\0');
        return bytes;
    }
}

int main()
{
    using namespace kh3ap;

    static_assert(runtime_olympus_coliseum_cups.size() == 24);
    static_assert(!runtime_olympus_coliseum_option_is_active(1));
    static_assert(runtime_olympus_coliseum_option_is_active(2));
    static_assert(runtime_olympus_coliseum_option_is_active(3));
    static_assert(!runtime_olympus_coliseum_option_is_active(4));
    std::set<std::wstring_view> cup_ids{};
    std::set<std::wstring_view> cup_names{};
    for (const RuntimeOlympusColiseumCup& cup :
         runtime_olympus_coliseum_cups)
    {
        assert(!cup.cup_id.empty());
        assert(!cup.series_name.empty());
        assert(!cup.cup_name.empty());
        assert(cup.cup_id ==
               std::wstring{cup.series_name} + L"/" +
                       std::wstring{cup.cup_name});
        assert(cup_ids.insert(cup.cup_id).second);
        assert(cup_names.insert(cup.cup_name).second);
    }

    RuntimeOlympusColiseumCompletionTracker tracker{};
    assert(tracker.completed_count() == 0);
    assert(tracker.mark_completed(L"series_kh1/cup_phil"));
    assert(!tracker.mark_completed(L"series_kh1/cup_phil"));
    assert(tracker.mark_completed(23));
    assert(!tracker.mark_completed(24));
    assert(!tracker.mark_completed(L"series_secret/cup_sephiroth"));
    assert(tracker.completed_count() == 2);
    const auto completed = tracker.completed_cup_ids();
    assert(completed.size() == 2);
    assert(completed.front() == L"series_kh1/cup_phil");
    assert(completed.back() == L"series_bbs/cup_aqua");

    tracker.reset();
    assert(tracker.completed_count() == 0);

    const RuntimeOlympusColiseumCup& hermes =
            runtime_olympus_coliseum_cups[11];
    const std::string incomplete_hermes = make_progress_entry(
            "series_kh3_cup_hermes_Normal",
            false);
    assert(!runtime_olympus_coliseum_save_has_completed_cup(
            incomplete_hermes,
            hermes));

    std::string completed_hermes = make_progress_entry(
            "series_kh3_cup_hermes_Normal",
            true);
    assert(runtime_olympus_coliseum_save_has_completed_cup(
            completed_hermes,
            hermes));
    assert(!runtime_olympus_coliseum_save_has_completed_cup(
            completed_hermes,
            runtime_olympus_coliseum_cups[12]));

    const std::string completed_apollo = make_progress_entry(
            "series_kh3_cup_apollo_Hard",
            true);
    const std::string completed_sephiroth = make_progress_entry(
            "series_secret_cup_sephiroth_Normal",
            true);
    assert(runtime_olympus_coliseum_completed_cup_indices(
                   completed_sephiroth).empty());
    completed_hermes.append(completed_sephiroth);
    completed_hermes.append(completed_apollo);
    const auto completed_indices =
            runtime_olympus_coliseum_completed_cup_indices(
                    completed_hermes);
    assert(completed_indices.size() == 2);
    assert(completed_indices[0] == 11);
    assert(completed_indices[1] == 12);
    return 0;
}
