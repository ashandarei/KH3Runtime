#include "kh3ap/features/enemies/remap_runtime.h"

namespace kh3ap::enemy_remap_detail
{
    RuntimeEnemyRemapState s_runtime_enemy_remap{};
    thread_local bool s_dispatching_runtime_boss_wipe_out{};
}
