#pragma once
#include "kh3ap/features/ui/map_overlay/lucky_emblem_marker.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeLuckyEmblemTrackerConfig
    {
        bool enabled{};
        std::function<std::optional<std::filesystem::path>()>
                bridge_dir_resolver{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::function<RC::Unreal::UObject*()> playable_pawn_resolver{};
        std::wstring_view build_stamp{};
        std::wstring_view state_filename{};
        std::wstring_view recorded_function_name{};
        std::wstring_view mark_id_property_name{};
        std::chrono::steady_clock::duration retry_interval{std::chrono::milliseconds(500)};
    };

    // Tracks the native Lucky Emblem components loaded in the current map and
    // publishes an additive exact-ID snapshot for the KH3 client. Components
    // are weakly held so map travel can never leave a stale raw UObject behind.
    class RuntimeLuckyEmblemTrackerService
    {
    public:
        explicit RuntimeLuckyEmblemTrackerService(
                RuntimeLuckyEmblemTrackerConfig config);
        ~RuntimeLuckyEmblemTrackerService();

        RuntimeLuckyEmblemTrackerService(
                const RuntimeLuckyEmblemTrackerService&) = delete;
        auto operator=(const RuntimeLuckyEmblemTrackerService&)
                -> RuntimeLuckyEmblemTrackerService& = delete;
        RuntimeLuckyEmblemTrackerService(
                RuntimeLuckyEmblemTrackerService&&) noexcept;
        auto operator=(RuntimeLuckyEmblemTrackerService&&) noexcept
                -> RuntimeLuckyEmblemTrackerService&;

        auto process_constructed_object(RC::Unreal::UObject* object) -> void;
        auto tick(std::chrono::steady_clock::time_point now) -> void;
        auto reset_map_lifecycle() -> void;
        auto notify_context_ready() -> void;
        // Game-thread, on-demand snapshot for the visible map. Reads native
        // completion afresh, independently of the additive bridge history.
        // Reconcile the resident native emblem actors on the map's existing
        // discovery gate. A detector may predate a lifecycle reset or become
        // attached to a streamed boat without a new construction observation.
        auto unrecorded_markers(
                RC::Unreal::UObject* world_context,
                bool rediscover,std::vector<RuntimeLuckyEmblemMarker>& markers) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
