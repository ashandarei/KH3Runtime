#include "kh3ap/features/ui/map_overlay/service_impl.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include <Unreal/UObject.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#endif
namespace kh3ap
{
    using namespace map_overlay_detail;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeMapOverlayService::diagnostic_probe(UObject* expected_pawn, bool toggle)
        -> std::string
    {
        auto* pawn = impl_->config.pawn ? impl_->config.pawn() : nullptr;
        if (!is_uobject_valid_fast(pawn) || pawn != expected_pawn)
        {
            throw std::runtime_error("map_overlay_current_hero_required");
        }
        const bool ready = impl_->config.gameplay_ready && impl_->config.gameplay_ready();
        bool consumed = false;
        if (toggle)
        {
            consumed = route(0, L"F8", 0, false);
            route(0, L"F8", 1, false);
        }
        auto* player = impl_->movie();
        auto* overlay = object(player, L"Root");
        const auto visible =
            overlay ? try_gfx_get_member_value(impl_->config.gfx_paths, overlay, L"visible") : L"";
        auto chests = nlohmann::json::array();
        for (const auto& chest : impl_->chests)
        {
            chests.push_back({{"treasure_id", to_utf8(chest.treasure)},
                              {"live", chest.actor.Get() != nullptr},
                              {"located", chest.located},
                              {"unopened", chest.unopened}});
        }
        const auto binding = reconciliation::view();
        return nlohmann::json{{"hero", to_utf8(pawn->GetFullName())},
                              {"ready", ready},
                              {"consumed", consumed},
                              {"opened", impl_->opened},
                              {"presented", impl_->presented},
                              {"pending", impl_->pending_toggle},
                              {"visible", to_utf8(visible)},
                              {"movie", to_utf8(impl_->movie_path)},
                              {"chests", std::move(chests)},
                              {"area_chests", impl_->displayed_icons},
                              {"minimap_chests", impl_->minimap_chests},
                              {"minimap_presented", impl_->minimap_presented},
                              {"save_binding",
                               {{"ready", binding->ready},
                                {"error", binding->error},
                                {"kh02_ready", binding->kh02_progress_ready},
                                {"kh02_error", binding->kh02_progress_error},
                                {"loaded_slot", binding->loaded_save_slot}}}}
            .dump();
    }
#endif
} // namespace kh3ap
