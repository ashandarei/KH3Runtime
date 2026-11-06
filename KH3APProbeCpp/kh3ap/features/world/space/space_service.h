#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    class RuntimeSpaceService
    {
    public:
        RuntimeSpaceService(
                std::function<std::optional<std::filesystem::path>()> bridge,
                std::function<RC::Unreal::UObject*()> sora_pawn);
        ~RuntimeSpaceService();
        void install_once();
        void tick(std::chrono::steady_clock::time_point now);
        // Game-thread only. Revalidates the current world and weak controller.
        static bool is_space_active();
        // Explicit save-point or Garden camp action, after the native menu has fully closed.
        static bool return_from_closed_menu(std::wstring_view expected_world);
        static RC::Unreal::UObject* item_delivery_context();
        static void notify_item_received(std::wstring_view token, bool show_notification = true);
        static void notify_map_ready();
        static void notify_context_changed();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        static RC::Unreal::UObject* diagnostic_flight_pawn(bool flight_only = true);
#endif
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
