#include "../../../KH3APProbeCpp/kh3ap/features/kh02/challenges/kh02_combat_event_journal.h"
#include <cassert>

int main()
{
    using Journal = kh3ap::Kh02CombatEventJournal<uint64_t>;
    using Observation = Journal::Observation;
    using namespace std::chrono_literals;
    Journal journal;
    const auto time = Journal::Clock::time_point{} + 10s;
    kh3ap::kh02_tracking::Hit hit;
    hit.player = true;
    hit.magic = true;
    // The native defeat callback occurs inside the original damage function.
    assert(journal.defeat(11, 11, L"enemy", time) == Observation::accepted);
    assert(journal.saw_defeat(11));
    hit.lethal = journal.saw_defeat(11);
    assert(journal.damage(11, 11, hit, 1, time + 150ms) == Observation::accepted);
    assert(journal.match(journal.deaths().front()).magic);
    assert(journal.defeat(11, 11, L"enemy", time) == Observation::ignored);
    assert(!journal.match({12, 12, L"other", time}).player);
    assert(!journal.match({11, 11, L"late", time - 1ms}).player);
    assert(journal.match({11, 11, L"early", time + 2150ms}).player);
    assert(!journal.match({11, 11, L"expired", time + 2151ms}).player);
    journal.retire_frame(time + 2150ms);
    assert(journal.deaths().empty());
    assert(journal.match({11, 11, L"boundary", time + 2150ms}).player);
    journal.retire_frame(time + 2151ms);
    assert(!journal.match({11, 11, L"expired", time + 2151ms}).player);
    journal.clear();
    hit.lethal = false;
    assert(journal.damage(11, 11, hit, 1, time) == Observation::accepted);
    assert(journal.damage(11, 11, hit, 0, time) == Observation::ignored);
    assert(!journal.match({11, 11, L"no hit", time}).player);
    for (uint64_t identity = 0; identity < Journal::death_history_limit; ++identity)
    {
        assert(journal.defeat(identity, identity, L"enemy", time) == Observation::accepted);
    }
    assert(journal.defeat(999, 999, L"overflow", time) == Observation::overflow);
    assert(journal.saw_defeat(999));
    journal.clear();
    assert(!journal.saw_defeat(999));
}
