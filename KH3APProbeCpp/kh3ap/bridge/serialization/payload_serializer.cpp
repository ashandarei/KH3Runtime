#include "kh3ap/bridge/serialization/payload_serializer.h"

#include "kh3ap/bridge/utilities.h"

#include <sstream>

namespace kh3ap
{
    auto RuntimePayloadSerializer::serialize_context(const RuntimeContextSnapshot& snapshot, uint32_t process_id) -> std::string
    {
        std::ostringstream json;
        json << "{\n";
        if (process_id)
        {
            json << "  \"process_id\": " << process_id << ",\n";
            json << "  \"recovery_protocol\": 1,\n";
            json << "  \"mapjump_auto_start_protocol\": 1,\n";
            json << "  \"kh02_gameflow_protocol\": 1,\n";
        }
        json << "  \"hook\": \"" << json_escape(snapshot.hook) << "\",\n";
        json << "  \"world\": \"" << json_escape(snapshot.world) << "\",\n";
        json << "  \"outer\": \"" << json_escape(snapshot.outer) << "\",\n";
        if (!snapshot.object.empty())
        {
            json << "  \"object\": \"" << json_escape(snapshot.object) << "\",\n";
        }
        if (!snapshot.pawn_source.empty())
        {
            json << "  \"pawn_source\": \"" << json_escape(snapshot.pawn_source) << "\",\n";
        }
        if (snapshot.sora_level.has_value())
        {
            json << "  \"sora_level\": " << *snapshot.sora_level << ",\n";
        }
        json << "  \"opened_at\": \"" << json_escape(snapshot.opened_at) << "\"\n";
        json << "}\n";
        return json.str();
    }

    auto RuntimePayloadSerializer::serialize_loot_payload(const RuntimeLootPayload& payload) -> std::string
    {
        std::ostringstream json;
        json << "{\n";
        json << "  \"observation_seq\": " << payload.observation_seq << ",\n";
        json << "  \"type\": \"" << json_escape(payload.type) << "\",\n";
        json << "  \"bridge_key\": \"" << json_escape(payload.bridge_key) << "\",\n";
        json << "  \"actor_name\": \"" << json_escape(payload.actor_name) << "\",\n";
        if (!payload.world_code.empty())
        {
            json << "  \"world_code\": \"" << json_escape(payload.world_code) << "\",\n";
        }
        json << "  \"source_id\": \"" << json_escape(payload.source_id) << "\",\n";
        json << "  \"source\": \"" << json_escape(payload.source) << "\",\n";
        if (!payload.level_path.empty())
        {
            json << "  \"level_path\": \"" << json_escape(payload.level_path) << "\",\n";
        }
        json << "  \"opened_at\": \"" << now_iso8601_utc() << "\",\n";
        json << "  \"grant_context\": \"" << json_escape(payload.grant_context) << "\",\n";
        json << "  \"encounter_hint\": \"" << json_escape(payload.encounter_hint) << "\",\n";
        json << "  \"mapping_reason\": \"" << json_escape(payload.mapping_reason) << "\",\n";
        json << "  \"resolution_kind\": \"" << json_escape(payload.resolution_kind) << "\",\n";
        if (!payload.discovery_route.empty())
        {
            json << "  \"discovery_route\": \"" << json_escape(payload.discovery_route) << "\",\n";
            json << "  \"discovery_source\": \"" << json_escape(payload.discovery_source) << "\",\n";
        }
        if (payload.has_treasure_route_telemetry)
        {
            json << "  \"treasure_route_observations\": {\n";
            json << "    \"treasure_logic_actor\": " << payload.treasure_route_observations.treasure_logic_actor << ",\n";
            json << "    \"open_treasure_player_state\": " << payload.treasure_route_observations.open_treasure_player_state << ",\n";
            json << "    \"open_treasure_char_state\": " << payload.treasure_route_observations.open_treasure_char_state << ",\n";
            json << "    \"other\": " << payload.treasure_route_observations.other << "\n";
            json << "  },\n";
            json << "  \"treasure_route_wins\": {\n";
            json << "    \"treasure_logic_actor\": " << payload.treasure_route_wins.treasure_logic_actor << ",\n";
            json << "    \"open_treasure_player_state\": " << payload.treasure_route_wins.open_treasure_player_state << ",\n";
            json << "    \"open_treasure_char_state\": " << payload.treasure_route_wins.open_treasure_char_state << ",\n";
            json << "    \"other\": " << payload.treasure_route_wins.other << "\n";
            json << "  },\n";
        }
        json << "  \"token_field_name\": \"" << json_escape(payload.token_field_name) << "\",\n";
        json << "  \"token_value\": \"" << json_escape(payload.token_value) << "\"";
        if (!payload.treasure_id.empty())
        {
            json << ",\n  \"treasure_id\": \"" << json_escape(payload.treasure_id) << "\"";
        }
        if (payload.is_large.has_value())
        {
            json << ",\n  \"is_large\": " << (*payload.is_large ? "true" : "false");
        }
        json << "\n";
        json << "}\n";
        return json.str();
    }
}
