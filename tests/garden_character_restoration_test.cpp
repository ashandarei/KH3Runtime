#include "kh3ap/features/world/garden_return/character_restoration.h"
#include "kh3ap/features/world/garden_return/runtime_context.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/core/events/world_context_events.h"
#include <array>
#include <cassert>
#include <vector>

namespace
{
    using namespace kh3ap;
    using namespace kh3ap::garden_return_detail;
    using RC::Unreal::UObject;

    std::array<std::byte, 0x900> instance_storage{};
    int pawn_storage{}, viewport_storage{}, wrong_pawn_storage{};
    auto* pawn = reinterpret_cast<UObject*>(&pawn_storage);
    auto* viewport_object = reinterpret_cast<UObject*>(&viewport_storage);
    auto* game_instance = reinterpret_cast<UObject*>(instance_storage.data());
    std::wstring current_world = L"World /Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown";
    int current_player = player_id_value(PlayerId::Sora);
    size_t context_reads{};
    bool loading_finished{true};
    bool interaction_pending{}, navigation_pending{}, combat_busy{}, camp_ready{};
    std::vector<int> requested_players;
    std::wstring phase;
}

namespace kh3ap
{
    bool is_uobject_valid_fast(const UObject* object) noexcept { return object == pawn; }
    bool runtime_sora_control_ready(UObject* object) { return object == pawn; }
    bool Kh02Story::challenges_available() { return true; }
    bool Kh02Story::challenges_open() { return false; }
}

namespace kh3ap::garden_return_detail
{
    UObject* object_field(UObject* object, const wchar_t* name)
    {
        return object == viewport_object && std::wstring_view{name} == L"GameInstance" ? game_instance : nullptr;
    }
    UObject* viewport() { return viewport_object; }
    std::wstring world() { ++context_reads; return current_world; }
    UObject* player() { ++context_reads; return pawn; }
    int player_id(UObject*) { return current_player; }
    bool loading_closed() { return loading_finished; }
    bool save_open() { return false; }
}

int main()
{
    using namespace kh3ap;
    using namespace kh3ap::garden_return_detail;
    const auto now = Clock::now();
    CharacterRestoration restoration({
        [] { return navigation_pending; },
        [] { return interaction_pending; },
        [] { return combat_busy; },
        [] { return camp_ready; },
        [](UObject* object, int id) { assert(object == pawn); requested_players.push_back(id); return true; },
        [](std::wstring_view next) { phase.assign(next); }});

    // The initial revision is observed, but an unselected character requires no
    // pawn/world resolution and no retry loop.
    restoration.tick(now);
    assert(context_reads == 0 && restoration.snapshot().checks == 0);
    restoration.select_aqua(true);
    restoration.tick(now);
    assert(requested_players == std::vector<int>{player_id_value(PlayerId::Aqua)});
    assert(phase == L"restoring_aqua_after_travel");
    restoration.tick(now + std::chrono::seconds(1));
    assert(requested_players.size() == 1); // Preserve the bounded retry deadline.
    current_player = player_id_value(PlayerId::Aqua);
    restoration.tick(now + std::chrono::seconds(2));
    assert(!restoration.snapshot().pending);
    const auto quiet_reads = context_reads;
    restoration.tick(now + std::chrono::seconds(3));
    assert(context_reads == quiet_reads);
    world_context_revision.fetch_add(1);
    restoration.tick(now + std::chrono::seconds(4));
    assert(context_reads > quiet_reads && !restoration.snapshot().pending);

    // A real arrival in ordinary KH3 dispatches Sora, then retires work only
    // after possession reports Sora. No AP-ready state is guessed at dispatch.
    current_world = L"World /Game/Levels/ts/ts_02/ts_02.ts_02";
    world_context_revision.fetch_add(1);
    restoration.tick(now + std::chrono::seconds(10));
    assert(requested_players.back() == player_id_value(PlayerId::Sora));
    assert(restoration.snapshot().aqua_selected && restoration.snapshot().pending);
    current_player = player_id_value(PlayerId::Sora);
    restoration.tick(now + std::chrono::seconds(11));
    assert(!restoration.snapshot().aqua_selected && !restoration.snapshot().pending);

    // An unfinished interaction blocks restoring Aqua; releasing that existing
    // operation allows the same pending request to continue without a new event.
    current_world = L"World /Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown";
    restoration.request(true);
    interaction_pending = true;
    const auto before_blocked = requested_players.size();
    restoration.tick(now + std::chrono::seconds(20));
    assert(requested_players.size() == before_blocked && restoration.snapshot().pending);
    interaction_pending = false;
    restoration.tick(now + std::chrono::seconds(21));
    assert(requested_players.size() == before_blocked + 1);

    // Startup's arrival path intentionally works under the loading cover, but
    // still requires the selected current pawn, matching world, and idle operation.
    loading_finished = false;
    requested_players.clear();
    restoration.defer_until(Clock::now() + std::chrono::hours(1));
    assert(!restoration.finish_arrival(pawn) && requested_players.empty());
    restoration.defer_until({});
    assert(!restoration.finish_arrival(reinterpret_cast<UObject*>(&wrong_pawn_storage)));
    assert(!restoration.finish_arrival(pawn));
    assert(requested_players == std::vector<int>{player_id_value(PlayerId::Aqua)});
    assert(phase == L"restoring_aqua_under_loading_cover");
    current_player = player_id_value(PlayerId::Aqua);
    assert(restoration.finish_arrival(pawn));
    interaction_pending = true;
    assert(!restoration.finish_arrival(pawn));
}
