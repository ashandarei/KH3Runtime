#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace kh3ap
{
    struct PortalSpawnResult
    {
        std::filesystem::path result_path{};
        std::filesystem::path events_path{};
        std::wstring protocol_name{};
        int protocol_version{};
        std::wstring stage{};
        std::wstring status{};
        std::wstring reason{};
        std::wstring source_name{};
        std::wstring request_id{};
        std::wstring build_stamp{};
        std::wstring map_path{};
        std::wstring context_key{};
        std::wstring runtime_context{};
        std::wstring pawn_name{};
        std::wstring spawned_actor_name{};
        std::wstring portal_class_path{};
        std::wstring portal_blueprint_path{};
        std::wstring class_resolution_source{};
        uintptr_t pawn_address{};
        uintptr_t spawned_actor_address{};
        uint64_t stable_ms{};
        double x{};
        double y{};
        double z{};
    };

    // Results own their text and addresses; the worker never accesses UObjects.
    auto write_portal_spawn_result(const PortalSpawnResult& result) -> void;
    auto enqueue_portal_spawn_result(PortalSpawnResult result) -> void;
    auto initialize_portal_result_writer() -> void;
}
