#include "kh3ap/features/player/emblems/lucky_emblem_record.h"
#include "kh3ap/bridge/checks/check_checkpoint.h"
#include <cassert>
#include <fstream>

using namespace kh3ap;

int main()
{
    using namespace emblem_record;
    struct RewardRow { int32_t milestone; int32_t reward_index; };
    const std::vector<RewardRow> rows{{1, 0}, {3, 1}, {5, 2}, {32, 31}, {33, 32}, {90, 89}};
    const std::array<int32_t, 2> completed{1, 3};
    const auto claims = claimed_rewards(completed, rows);
    assert(claims == 3); // Old-save zero claims restores 1 and 3, leaves 5 eligible.
    assert((claims & (1u << 2)) == 0);
    assert((claims | claims) == claims); // Repeated restore cannot announce again.
    const std::array<int32_t, 3> later{32, 33, 90};
    assert(claimed_rewards(later, rows) == 0x80000001u);
    assert(claimed_rewards(std::span<const int32_t>{}, rows) == 0);
    const auto pumpkin = from_checkpoint({L"89"});
    const auto other = from_checkpoint({L"1", L"32", L"33", L"64", L"65", L"90"});
    const auto remembered = merge(pumpkin, other);
    assert(checkpoint_ids(remembered).size() == 7);
    // Repeated photographs and older checkpoints cannot reduce or inflate history.
    assert(merge(remembered, {}) == remembered);
    assert(merge(remembered, pumpkin) == remembered);
    assert(merge(remembered, other) == remembered);
    assert(merge({}, {0, 0, 0xfc000000}) == Words{});
    Words all{0xffffffff, 0xffffffff, last_word_mask};
    assert(checkpoint_ids(all).size() == 90);
    assert(from_checkpoint(checkpoint_ids(all)) == all);
    for (const auto* bad : {L"0", L"91", L"96", L"01", L"-1", L"", L"1.0", L"x"})
    {
        bool rejected = false;
        try { from_checkpoint({bad}); } catch (const std::exception&) { rejected = true; }
        assert(rejected);
    }

    const auto path = std::filesystem::temp_directory_path() / "kh3-emblem-checkpoint-test.json";
    const auto save = [&](const char* body) { std::ofstream out(path); out << body; };
    constexpr auto schema = L"kh3ap.lucky_emblem_native_indices.v1";
    save(R"({"schema":"kh3ap.lucky_emblem_native_indices.v1","seed_name":"seed-a","slot":1,"completed":["88","90"]})");
    auto restored = from_checkpoint(load_check_checkpoint(path, schema, L"seed-a", 1));
    assert(restored == from_checkpoint({L"88", L"90"}));
    for (const auto& scope : {std::pair{L"seed-b", 1}, std::pair{L"seed-a", 2}})
    {
        bool rejected = false;
        try { load_check_checkpoint(path, schema, scope.first, scope.second); }
        catch (const std::exception&) { rejected = true; }
        assert(rejected);
    }
    save("{truncated");
    bool rejected = false;
    try { load_check_checkpoint(path, schema, L"seed-a", 1); }
    catch (const std::exception&) { rejected = true; }
    assert(rejected);
    std::filesystem::remove(path);
    assert(load_check_checkpoint(path, schema, L"seed-a", 1).empty());
}
