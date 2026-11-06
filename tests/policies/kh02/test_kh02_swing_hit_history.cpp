#include "kh3ap/features/kh02/combat/kh02_swing_hit_history.h"
#include <cassert>

int main()
{
    kh3ap::Kh02SwingHitHistory hits;
    hits.begin();
    hits.record(1);
    assert(!hits.blocked(1)); // Native multi-hit behavior within one hero is unchanged.
    hits.resume();
    assert(hits.blocked(1));
    assert(!hits.blocked(2)); // A new enemy can still be struck after the swap.
    hits.record(2);
    assert(!hits.blocked(2));
    hits.resume();
    assert(hits.blocked(1) && hits.blocked(2)); // Repeated swaps cannot re-hit either enemy.
    assert(!hits.blocked(0x100000001ULL));      // Recycled UObject index, different weak serial.
    hits.begin();
    assert(!hits.blocked(1) && !hits.blocked(2)); // The next real swing can hit normally.
}
