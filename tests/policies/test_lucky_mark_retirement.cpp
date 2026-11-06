#include "kh3ap/hooks/lucky_marks/lucky_mark_retirement.h"

#include <cassert>

int main()
{
    static_assert(kh3ap::lucky_mark_native_ap_display_row_matches(712, 712));
    static_assert(kh3ap::lucky_mark_native_ap_display_row_matches(767, 767));
    static_assert(kh3ap::lucky_mark_native_ap_display_row_matches(518, 518));
    static_assert(kh3ap::lucky_mark_native_ap_display_row_matches(522, 522));
    static_assert(kh3ap::lucky_mark_native_ap_display_row_matches(711, 711));
    // Unmodified vanilla rows and mismatched AP keys must keep native text.
    static_assert(!kh3ap::lucky_mark_native_ap_display_row_matches(518, std::nullopt));
    static_assert(!kh3ap::lucky_mark_native_ap_display_row_matches(518, 519));
    static_assert(!kh3ap::lucky_mark_native_ap_display_row_matches(517, 517));
    static_assert(!kh3ap::lucky_mark_native_ap_display_row_matches(768, 768));

    // Native success must still send/retire an AP milestone, including rewards
    // beyond the game's claim slots. Preserve the native ID for presentation.
    constexpr auto native_ap = kh3ap::resolve_lucky_mark_native_ap_reward(762, 762);
    static_assert(native_ap.item_id == 762 && native_ap.process_ap_check);
    constexpr auto native_remapped = kh3ap::resolve_lucky_mark_native_ap_reward(517, 762);
    static_assert(native_remapped.item_id == 517 && native_remapped.process_ap_check);
    constexpr auto fallback_ap = kh3ap::resolve_lucky_mark_native_ap_reward(0, 762);
    static_assert(fallback_ap.item_id == 762 && fallback_ap.process_ap_check);
    constexpr auto vanilla = kh3ap::resolve_lucky_mark_native_ap_reward(517, std::nullopt);
    static_assert(vanilla.item_id == 517 && !vanilla.process_ap_check);
    constexpr auto unknown = kh3ap::resolve_lucky_mark_native_ap_reward(0, std::nullopt);
    static_assert(unknown.item_id == 0 && !unknown.process_ap_check);

    static_assert(kh3ap::lucky_mark_native_claim_slot_count == 32);
    static_assert(kh3ap::lucky_mark_native_retired_alias_index == 0);
    static_assert(!kh3ap::lucky_mark_reward_index_needs_retirement(31));
    static_assert(kh3ap::lucky_mark_reward_index_needs_retirement(32));

    assert(kh3ap::try_parse_lucky_mark_milestone_row_name(L"33") == 33);
    assert(!kh3ap::try_parse_lucky_mark_milestone_row_name(L"0").has_value());
    assert(!kh3ap::try_parse_lucky_mark_milestone_row_name(L"33x").has_value());

    return 0;
}
