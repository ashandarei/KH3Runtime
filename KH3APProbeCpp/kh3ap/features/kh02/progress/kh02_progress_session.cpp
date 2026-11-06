#include "kh02_progress_session.h"
#include "kh02_progress_policy.h"
#include "kh3ap/features/kh02/story/kh02_story_flags.h"
#include "kh3ap/features/kh02/wardrobe/kh02_dressing_room.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_tracker.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_reconciliation.h"
#include "kh3ap/features/kh02/progress/kh02_story_checkpoints.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace kh3ap
{
    using RC::Unreal::UObject;

    Kh02ProgressSession::Kh02ProgressSession(Kh02StoryFlags& flags, Kh02DressingRoom& wardrobe,
                                             Kh02ChallengeTracker& tracker, std::map<int, int>& challenge_states,
                                             std::map<int, int>& challenge_counts,
                                             std::map<int, bool>& challenge_events)
        : source_flags(flags), dressing_room(wardrobe), challenge_tracker(tracker), challenges(challenge_states),
          counts(challenge_counts), events(challenge_events)
    {
    }

    void Kh02ProgressSession::publish()
    {
        try
        {
            if (!source_flags.active() || source_flags.value(L"gameflow_DW") < kh02_checkpoint::gear_scene_dw009 ||
                binding_revision == UINT64_MAX)
            {
                return;
            }
            auto flags = source_flags.progress_values();
            kh02_reconcile_challenges(flags, challenges, counts);
            kh02_progress::Snapshot value{std::move(flags), challenges, counts, events,
                                          dressing_room.progress_selection()};
            kh02_progress::validate(value);
            reconciliation::kh02_progress_update(std::move(value), binding_revision, binding_seed, binding_player_slot);
            publish_error.clear();
        }
        catch (const std::exception& e)
        {
            publish_error = e.what();
        }
    }

    auto Kh02ProgressSession::bind(UObject* world, int checkpoint, bool backtrack) -> Entry
    {
        const auto saved = reconciliation::view();
        if (!saved->kh02_progress_ready)
        {
            throw std::runtime_error("kh02_progress_restore_not_ready:" + saved->error + saved->kh02_progress_error);
        }
        const int flow = source_flags.value(L"gameflow_DW");
        const bool same = matches(saved->seed, saved->slot, saved->kh02_progress_revision);
        if (same && checkpoint >= kh02_checkpoint::mirror_scene_dw015 && flow >= kh02_checkpoint::mirror_scene_dw015)
        {
            source_flags.rebind_mirror(world);
            if (checkpoint == kh02_checkpoint::mirror_scene_dw015)
            {
                source_flags.reset_mirror_hub_entry();
            }
            return {backtrack, flow >= kh02_checkpoint::mirror_scene_dw016};
        }
        const auto& value = saved->kh02_progress;
        was_restored = static_cast<bool>(value);
        // A Castle Town story replay starts its story again, while challenge
        // counts and collected mirror flags remain in the same native slot.
        const auto flags = same ? source_flags.progress_values() : value ? value->flags : kh02_progress::Flags{};
        source_flags.activate(world, checkpoint, true);
        if (!same)
        {
            dressing_room.restore(value ? value->cosmetics : std::nullopt);
            const auto equipped = dressing_room.progress_selection().equipped;
            challenge_tracker.wardrobe(
                std::all_of(equipped.begin(), equipped.end(), [](const auto& item) { return item != 0; }));
            challenges.clear();
            counts.clear();
            events.clear();
            if (was_restored)
            {
                challenges = value->challenges;
                counts = value->counts;
                // Event windows belong to live encounters, not a prior process.
            }
        }
        if (!flags.empty())
        {
            if (checkpoint == kh02_checkpoint::gear_scene_dw011 &&
                kh02_progress::flag_value(flags, "gameflow_DW") < kh02_checkpoint::gear_scene_dw011)
            {
                // Area Select also requests backtracking. An interrupted
                // opening must still replay its missing source continuation.
                backtrack = false;
            }
            // A revisit through GoA or a native Castle Town save must preserve
            // the same story bank as Area Select. Fresh entries still start
            // the original introduction; explicit replay fixtures stay separate.
            if (checkpoint == kh02_checkpoint::gear_scene_dw011 && kh02_progress::resume_castle_entry(flags))
            {
                backtrack = true;
            }
            auto restored = kh02_progress::enter_flags(flags, checkpoint,
                                                       checkpoint == kh02_checkpoint::gear_scene_dw011 && backtrack);
            source_flags.restore_mirror(restored);
        }
        binding_revision = saved->kh02_progress_revision;
        binding_seed = saved->seed;
        binding_player_slot = saved->slot;
        const bool resume = checkpoint >= kh02_checkpoint::mirror_scene_dw015 &&
                            source_flags.value(L"gameflow_DW") >= kh02_checkpoint::mirror_scene_dw016;
        if (checkpoint == kh02_checkpoint::mirror_scene_dw015)
        {
            source_flags.reset_mirror_hub_entry();
        }
        publish();
        return {backtrack, resume};
    }

    void Kh02ProgressSession::invalidate()
    {
        binding_revision = UINT64_MAX;
        binding_seed.clear();
        binding_player_slot = 0;
        was_restored = false;
    }

    auto Kh02ProgressSession::matches(const std::wstring& seed, int64_t player_slot, uint64_t revision) const -> bool
    {
        return binding_seed == seed && binding_player_slot == player_slot && binding_revision == revision;
    }
} // namespace kh3ap
