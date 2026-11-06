# Custom UI

This feature owns the custom options interface and its interaction with the game.

- `goa_options_menu.h`: public menu builders.
- `goa_settings_menu.cpp`, `goa_randomization_menu.cpp`, `goa_music_menu.cpp`,
  `goa_mom_menu.cpp`, `goa_credits_menu.cpp`: feature-owned page builders and helpers.
- `options_menu.h/.cpp`: menu definitions, pages, controls and builders.
- `options_menu_service.h/.cpp`: native menu lifecycle, rendering and interaction.
- `game_settings_shortcut.h/.cpp`: access to the game's settings from custom menus.
- `ui_sound_service.h/.cpp`: UI sound dispatch used by menus and other features.

Include headers through `kh3ap/features/ui/...`. Shared preferences and input
ownership primitives are maintained separately in `core/`.
