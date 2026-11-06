#include "kh3ap/features/ui/map_overlay/service_impl.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <utility>
namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace map_overlay_detail;

    RuntimeMapOverlayService::RuntimeMapOverlayService(RuntimeMapOverlayConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }
    RuntimeMapOverlayService::~RuntimeMapOverlayService() = default;

    auto RuntimeMapOverlayService::tick() -> void
    {
        impl_->tick();
    }
    auto RuntimeMapOverlayService::notify_treasure_changed() -> void
    {
        impl_->chest_schedule.changed();
        impl_->next_chests = {};
        impl_->next_minimap_markers = {};
    }
    auto RuntimeMapOverlayService::notify_chest_changed(const FWeakObjectPtr& chest) -> void
    {
        impl_->chest_discovery.notify(chest);
        impl_->next_chests = {};
        impl_->next_minimap_markers = {};
    }
    auto RuntimeMapOverlayService::reset_map_lifecycle() -> void
    {
        impl_->chests.clear();
        impl_->chest_world = {};
        impl_->chest_discovery.request_scan();
        notify_treasure_changed();
    }
    auto RuntimeMapOverlayService::route(int controller, std::wstring_view key, int event,
                                         bool gamepad) -> bool
    {
        if (gamepad && (key == L"Gamepad_LeftTrigger" || key == L"Gamepad_RightTrigger"))
        {
            bool changed = false, toggle_locked = false;
            int zoom_direction = 0;
            const bool consumed = impl_->triggers.route(
                controller, key == L"Gamepad_RightTrigger" ? 1 : -1, event,
                impl_->opened && impl_->presented && impl_->enabled() &&
                    impl_->config.gameplay_ready && impl_->config.gameplay_ready(),
                zoom_direction, toggle_locked);
            if (zoom_direction)
            {
                impl_->zoom.route(controller, zoom_direction, 0, true, changed);
                bool released = false;
                impl_->zoom.route(controller, zoom_direction, 1, true, released);
            }
            if (toggle_locked && impl_->config.set_icon_option)
            {
                impl_->config.set_icon_option(MapIconOption::LockedChests,
                                              !impl_->icon_enabled(MapIconOption::LockedChests));
                impl_->next_chests = {};
                impl_->next_minimap_markers = {};
            }
            if (changed)
            {
                impl_->next_tick = {};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Area map zoom key={} factor={} chest_icon_factor={}\n"),
                    key, impl_->zoom.factor(), impl_->zoom.chest_factor());
#endif
            }
            return consumed;
        }
        if (key != (gamepad ? L"Gamepad_LeftThumbstick" : L"F8"))
        {
            return false;
        }
        bool toggle = false;
        // Resolve shortcut eligibility only on a press, not ten times a second.
        if (event == 0)
        {
            impl_->eligible = false;
            if (impl_->enabled() && impl_->config.gameplay_ready && impl_->config.gameplay_ready())
            {
                auto* pawn = impl_->config.pawn ? impl_->config.pawn() : nullptr;
                const auto [hud, nav] = live_parts(pawn);
                impl_->eligible =
                    root(nav) && object(nav, L"m_LoadedAsyncTexture") && object(nav, L"m_SwfMovie");
            }
        }
        bool eligible = impl_->eligible && impl_->enabled();
        if (impl_->input.pending())
        {
            auto* p = impl_->config.pawn ? impl_->config.pawn() : nullptr;
            eligible = eligible && p && impl_->hold_pawn.Get() == p &&
                       impl_->config.gameplay_ready && impl_->config.gameplay_ready();
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (event == 0)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Area map input key={} eligible={}\n"), key, eligible);
        }
#endif
        const bool was_pending = impl_->input.pending();
        const bool consumed =
            impl_->input.route(controller, !gamepad, event, eligible, impl_->opened,
                               std::chrono::steady_clock::now(), toggle);
        if (!was_pending && impl_->input.pending())
        {
            impl_->hold_pawn = impl_->config.pawn ? impl_->config.pawn() : nullptr;
        }
        if (toggle)
        {
            impl_->pending_toggle = !impl_->pending_toggle;
        }
        return consumed;
    }
} // namespace kh3ap
