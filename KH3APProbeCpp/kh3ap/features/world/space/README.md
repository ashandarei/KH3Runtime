# Space runtime

This subfeature owns Gummi space navigation and the boundary between Sora gameplay
and the space overview, stations, encounters and landing.

- `space_service.h/.cpp`, `space_service_impl.h`: public service and internal composition.
- `space_context.cpp`, `space_presentation.cpp`: live context and map/UI presentation.
- `space_access.h/.cpp`: seed-bound world access snapshots.
- `space_encounters.cpp`, `space_battle_markers.h/.cpp`: encounter and marker handling.
- `space_landing.h/.cpp`, `space_embark_help.cpp`: landing and Embark behavior.
- `space_ships.h/.cpp`, `space_ship_catalog.h`: ship management and generated ship data.
- `space_rewards.h/.cpp`, `space_notifications.h/.cpp`: rewards and notices.
- `space_diagnostics.cpp`, `space_battle_diagnostics.cpp`: diagnostic commands.
- `space_*_policy.h`, `space_policy.h`, `space_refresh_state.h`: policy and event models.
- `space_profile.h`, `space_egs_profile.h`: supported native build layouts.
- `save_space_service.h/.cpp`, `save_space_policy.h`: save-point travel into space.

Include headers through `kh3ap/features/world/space/...`. The ship catalog generator,
explicit CMake sources and focused space tests all reference this directory.
