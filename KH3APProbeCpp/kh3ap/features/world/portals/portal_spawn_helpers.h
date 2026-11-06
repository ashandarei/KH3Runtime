#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UClass;
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    struct RuntimePortalAutoSpawnContext
    {
        bool latest_context_matches_target_map{};
        bool pawn_matches_target_map{};
        bool used_pause_menu_fallback{};
        bool used_prior_playable_context{};
        std::wstring context_key{};

        [[nodiscard]] auto target_map_is_current() const -> bool
        {
            return latest_context_matches_target_map && pawn_matches_target_map;
        }

        [[nodiscard]] auto can_progress() const -> bool
        {
            return target_map_is_current() && !context_key.empty();
        }
    };

    [[nodiscard]] inline auto runtime_portal_context_matches_map(
            std::wstring_view context_text,
            std::wstring_view map_path) -> bool
    {
        if (map_path.empty())
        {
            return false;
        }
        if (context_text.find(map_path) != std::wstring_view::npos)
        {
            return true;
        }

        const std::size_t last_slash = map_path.rfind(L'/');
        if (last_slash == std::wstring_view::npos || last_slash == 0)
        {
            return false;
        }
        const std::wstring map_root_prefix = std::wstring{map_path.substr(0, last_slash)} + L"/";
        return context_text.find(map_root_prefix) != std::wstring_view::npos;
    }

    [[nodiscard]] inline auto resolve_runtime_portal_auto_spawn_context(
            std::wstring_view latest_hook,
            std::wstring_view latest_world,
            std::wstring_view latest_outer,
            std::wstring_view latest_object,
            const void* latest_playable_object,
            std::wstring_view pawn_name,
            const void* pawn_object,
            std::wstring_view map_path,
            std::wstring_view map_ready_context_key,
            std::wstring_view prior_playable_context_key) -> RuntimePortalAutoSpawnContext
    {
        RuntimePortalAutoSpawnContext result{};
        const std::wstring latest_context =
                std::wstring{latest_world} + L" " + std::wstring{latest_outer} + L" " + std::wstring{latest_object};
        result.latest_context_matches_target_map = runtime_portal_context_matches_map(latest_context, map_path);
        result.pawn_matches_target_map = runtime_portal_context_matches_map(pawn_name, map_path);
        if (!result.target_map_is_current())
        {
            return result;
        }

        if (latest_hook == L"RuntimePlayablePawnContext" &&
            !latest_world.empty() &&
            latest_playable_object &&
            latest_playable_object == pawn_object)
        {
            result.context_key = std::wstring{latest_world} + L"|" + std::wstring{latest_object};
            return result;
        }

        const bool map_ready_matches_pawn =
                !pawn_name.empty() &&
                runtime_portal_context_matches_map(map_ready_context_key, map_path) &&
                map_ready_context_key.find(pawn_name) != std::wstring_view::npos;
        if (latest_hook == L"TresSetPauseMenuType" && map_ready_matches_pawn)
        {
            // TresSetPauseMenuType can be published from a streamed camera
            // sublevel after the base-map pawn and map-ready signal are valid.
            // Spawning still uses the cached pawn as the UE world context.
            result.used_pause_menu_fallback = true;
            result.context_key = std::wstring{map_ready_context_key};
            return result;
        }

        const bool prior_playable_context_matches_pawn =
                !pawn_name.empty() &&
                runtime_portal_context_matches_map(prior_playable_context_key, map_path) &&
                prior_playable_context_key.find(pawn_name) != std::wstring_view::npos;
        if (latest_hook == L"TresSetPauseMenuType" && prior_playable_context_matches_pawn)
        {
            // Some BX03 -> BX02 story transitions never publish the normal
            // map-ready callback. Permit the streamed-camera pause context only
            // after this lifecycle has already observed the same playable pawn.
            result.used_pause_menu_fallback = true;
            result.used_prior_playable_context = true;
            result.context_key = std::wstring{prior_playable_context_key};
        }
        return result;
    }

    // Generated classes used by runtime-spawned actors are tiny, process-wide
    // assets. The portal and world-bootstrap paths share this cache so map
    // travel cannot unload a class between spawns. The pointer is protected by
    // that root and is still slot-validated before every use.
    class RuntimePortalClassCache
    {
    public:
        [[nodiscard]] auto retained_class() const -> RC::Unreal::UClass*;
        auto retain(RC::Unreal::UClass* portal_class) -> bool;

    private:
        RC::Unreal::UClass* retained_class_{};
    };

    inline constexpr uint32_t runtime_portal_max_class_load_requests = 3;

    [[nodiscard]] inline constexpr auto runtime_portal_class_load_request_allowed(
            uint32_t request_count) noexcept -> bool
    {
        return request_count < runtime_portal_max_class_load_requests;
    }

    struct RuntimeAutoPortalSpawnState;

    struct RuntimePortalSpawnDeps
    {
        RuntimeState& runtime_state;
        std::optional<std::filesystem::path> bridge_dir;
        std::wstring_view request_filename;
        std::wstring_view result_filename;
        std::wstring_view events_filename;
        std::wstring_view protocol_name;
        int protocol_version{};
        std::wstring_view build_stamp;
        std::wstring_view portal_class_path;
        std::wstring_view portal_blueprint_path;
        std::wstring_view actor_location_function_path;
        bool debug_logging{};
        RuntimePortalClassCache* class_cache{};
    };

    auto process_pending_runtime_portal_spawn(const RuntimePortalSpawnDeps& deps) -> void;

    auto process_runtime_map_portal_auto_spawn(
            RuntimeAutoPortalSpawnState& state,
            const RuntimePortalSpawnDeps& deps,
            std::chrono::steady_clock::time_point now,
            std::wstring_view source_name,
            std::wstring_view request_id_prefix,
            std::wstring_view map_path,
            double x,
            double y,
            double z,
            std::chrono::steady_clock::duration stable_delay,
            std::chrono::steady_clock::duration retry_interval,
            std::chrono::steady_clock::duration verification_delay,
            std::wstring_view map_ready_context_key,
            bool spawn_eligible = true,
            std::wstring_view ineligibility_reason = {}) -> void;
}
