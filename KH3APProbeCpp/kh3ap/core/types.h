#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UFunction;
}

namespace kh3ap
{
    struct MapHeader
    {
        uint64_t data_ptr{};
        int32_t num{};
        int32_t max{};
    };

    struct ArrayHeader
    {
        uint64_t data_ptr{};
        int32_t num{};
        int32_t max{};
    };

    struct TreasureRouteCounts
    {
        uint64_t treasure_logic_actor{};
        uint64_t open_treasure_player_state{};
        uint64_t open_treasure_char_state{};
        uint64_t other{};
    };

    struct RuntimeLootPayload
    {
        uint64_t observation_seq{0};
        std::wstring type;
        std::wstring bridge_key;
        std::wstring actor_name;
        std::wstring world_code;
        std::wstring source_id;
        std::wstring source;
        std::wstring level_path;
        std::wstring opened_at;
        std::wstring grant_context;
        std::wstring encounter_hint;
        std::wstring mapping_reason;
        std::wstring resolution_kind;
        std::wstring token_field_name;
        std::wstring token_value;
        std::wstring treasure_id;
        std::optional<bool> is_large{};
        std::wstring discovery_route;
        std::wstring discovery_source;
        TreasureRouteCounts treasure_route_observations{};
        TreasureRouteCounts treasure_route_wins{};
        bool has_treasure_route_telemetry{false};
    };

    struct RuntimeContextSnapshot
    {
        std::wstring hook;
        std::wstring world;
        std::wstring outer;
        std::wstring object;
        std::wstring pawn_source;
        std::wstring opened_at;
        std::optional<int64_t> sora_level{};
    };

    struct RuntimeItemGrantRequest
    {
        int64_t index{-1};
        std::wstring request_id{};
        std::wstring item_name{};
        std::wstring runtime_token{};
        std::wstring runtime_flag_name{};
        std::wstring runtime_vbonus_source_id{};
        std::wstring grant_method{};
        std::wstring from_player_name{};
        std::wstring location_name{};
        std::wstring action_kind{};
        std::wstring reapply_context_key{};
        std::vector<std::wstring> randomized_tokens{};
        std::vector<std::wstring> bundle_display{};
        std::vector<std::wstring> vanilla_tokens{};
        std::wstring randomized_summary{};
        std::wstring vanilla_summary{};
        std::wstring event_gameflow_flag{L"None"};
        std::wstring event_gameflow_source{L"None"};
        std::wstring event_original_item{L"AP_RUNTIME_ITEM"};
        int64_t munny_delta{0};
        bool show_info{true};
    };

    struct RuntimeItemGrantBatchRequest
    {
        std::wstring request_schema{};
        int64_t process_id{};
        int64_t delivery_epoch{-1};
        std::wstring seed_name{};
        int64_t slot{};
        std::wstring request_id{};
        std::wstring action_kind{};
        std::wstring reapply_context_key{};
        std::vector<RuntimeItemGrantRequest> grants{};
        bool expand_catalog{};
    };

    struct RuntimeApMessageRequest
    {
        std::wstring request_id{};
        std::wstring message{};
        std::wstring bridge_key{};
        std::wstring location_name{};
        std::wstring item_name{};
        double duration_seconds{6.0};
    };

    struct RuntimeItemApplyPayload
    {
        int64_t index{-1};
        std::wstring item_name{};
        std::wstring request_id{};
        std::wstring action_kind{};
        std::wstring runtime_token{};
        std::wstring grant_method{};
        std::wstring status{};
        std::wstring error{};
        int64_t runtime_request_wait_ms{-1};
        int64_t runtime_grant_apply_elapsed_ms{-1};
    };

    struct RuntimeItemApplyBatchPayload
    {
        std::wstring request_id{};
        std::wstring action_kind{};
        std::wstring status{};
        std::wstring error{};
        std::vector<RuntimeItemApplyPayload> applied{};
        int64_t runtime_request_wait_ms{-1};
        int64_t runtime_grant_apply_elapsed_ms{-1};
        bool request_complete{true};
        int64_t process_id{}, delivery_epoch{-1}, slot{};
        std::wstring seed_name{};
    };

    struct HookTargetInfo
    {
        std::wstring function_path;
        std::wstring function_name;
        std::wstring token_field_name;
        std::wstring return_field_name;
        RC::Unreal::UFunction* function_object{nullptr};
    };

    struct VBonusLevelActorSnapshotEntry
    {
        std::wstring full_name;
        std::wstring level_name;
        std::wstring vbonus_name;
    };

    struct DecodedNameValue
    {
        uint32_t low32{0};
        uint32_t high32{0};
        std::wstring fname_first{};
        std::wstring fname_pair{};
    };

    struct TreasureMappingResult
    {
        std::wstring bridge_key;
        std::wstring mapping_reason;
        std::wstring world_code;
        std::wstring source_id;
    };
}
