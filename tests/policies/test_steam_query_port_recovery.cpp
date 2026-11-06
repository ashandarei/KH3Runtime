#include "kh3ap/core/platform/steam_query_port_recovery.h"

#include <cassert>
#include <optional>

int main()
{
    using kh3ap::decide_runtime_steam_query_port;
    using kh3ap::s_kh3_steam_app_id;

    constexpr uint16_t requested_port = 27015;
    constexpr uint16_t game_port = 7777;
    constexpr uint16_t candidate_port = 50156;

    const auto normal = decide_runtime_steam_query_port(
            true,
            true,
            s_kh3_steam_app_id,
            requested_port,
            game_port,
            true,
            std::nullopt);
    assert(normal.effective_port == requested_port);
    assert(!normal.replaced);

    const auto recovered = decide_runtime_steam_query_port(
            true,
            true,
            s_kh3_steam_app_id,
            requested_port,
            game_port,
            false,
            candidate_port);
    assert(recovered.effective_port == candidate_port);
    assert(recovered.replaced);

    for (const auto unchanged : {
                 decide_runtime_steam_query_port(
                         false,
                         true,
                         s_kh3_steam_app_id,
                         requested_port,
                         game_port,
                         false,
                         candidate_port),
                 decide_runtime_steam_query_port(
                         true,
                         false,
                         s_kh3_steam_app_id,
                         requested_port,
                         game_port,
                         false,
                         candidate_port),
                 decide_runtime_steam_query_port(
                         true,
                         true,
                         0,
                         requested_port,
                         game_port,
                         false,
                         candidate_port),
                 decide_runtime_steam_query_port(
                         true,
                         true,
                         s_kh3_steam_app_id,
                         0,
                         game_port,
                         false,
                         candidate_port),
                 decide_runtime_steam_query_port(
                         true,
                         true,
                         s_kh3_steam_app_id,
                         requested_port,
                         game_port,
                         false,
                         std::nullopt),
                 decide_runtime_steam_query_port(
                         true,
                         true,
                         s_kh3_steam_app_id,
                         requested_port,
                         game_port,
                         false,
                         requested_port),
                 decide_runtime_steam_query_port(
                         true,
                         true,
                         s_kh3_steam_app_id,
                         requested_port,
                         game_port,
                         false,
                         game_port)})
    {
        assert(!unchanged.replaced);
    }

    return 0;
}
