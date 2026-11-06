#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/app/composition/bindings.h"
#include "kh3ap/app/composition/settings.h"
#include "kh3ap/features/player/keyblades/style_change_mode.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/core/settings/goa_settings_service.h"
#include "kh3ap/features/story/minigames/beat_of_lead_completion.h"
#include "kh3ap/features/story/world_fixes/festival_dance_story_patch.h"
#include "kh3ap/features/story/cutscenes/guardians_of_light_skip.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/features/story/minigames/union_cross_completion.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap::composition_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace settings;
    auto bridge_path_resolver() -> kh3ap::BridgePathResolver&
    {
        static kh3ap::BridgePathResolver resolver{
            std::wstring{s_default_output_path},
            std::wstring{s_probe_config_path},
            s_bridge_dir_cache_ttl,
            s_debug_logging,
        };
        return resolver;
    }

    auto get_output_paths() -> const std::vector<std::filesystem::path>&
    {
        return bridge_path_resolver().output_paths();
    }

    auto try_get_bridge_dir() -> std::optional<std::filesystem::path>
    {
        return bridge_path_resolver().try_get_bridge_dir();
    }

    auto auto_skip_cutscenes_enabled_from_seed() -> bool
    {
        return seed_bool_option(installed_seed_slot_data(), "auto_skip_cutscenes");
    }

    auto style_change_mode_from_seed() -> kh3ap::RuntimeStyleChangeMode
    {
        return resolve_runtime_style_change_mode(installed_seed_slot_data().int64_field("stylesanity"));
    }

    auto runtime_toggle_enabled_from_seed(std::string_view option_name) -> bool
    {
        return seed_bool_option(installed_seed_slot_data(), option_name);
    }

    auto guardians_and_mickey_walk_skip_enabled_from_seed() -> bool
    {
        return runtime_toggle_enabled_from_seed(kh3ap::runtime_guardians_and_mickey_walk_skip_slot_option);
    }

    auto fast_dance_minigame_enabled_from_seed() -> bool
    {
        return runtime_toggle_enabled_from_seed(kh3ap::runtime_festival_dance_story_slot_option);
    }

    auto fast_beat_of_lead_enabled_from_seed() -> bool
    {
        return runtime_toggle_enabled_from_seed(kh3ap::runtime_beat_of_lead_slot_option);
    }

    auto map_overlay_keybinds_option_from_seed() -> bool
    {
        return seed_bool_option(installed_seed_slot_data(), "map_overlay_keybinds", true);
    }

    auto fast_union_cross_enabled_from_seed() -> bool
    {
        return runtime_toggle_enabled_from_seed(kh3ap::runtime_union_cross_slot_option);
    }
} // namespace kh3ap::composition_detail
