#include "kh3ap/app/config.h"
#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/grants/battle_level/battle_level_sync_service.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <array>
#include <format>
#include <sstream>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;
    namespace
    {
        using ::kh3ap::settings::s_bridge_protocol_name;
        using ::kh3ap::settings::s_bridge_protocol_version;
    }

    auto RuntimePlayerContextService::Impl::write_new_game_staging_current_slot(
        std::wstring_view source, const RuntimeContextSnapshot& snapshot) -> void
    {
        const auto resolved_bridge_dir = bridge_dir();
        if (!resolved_bridge_dir.has_value())
        {
            return;
        }

        const std::string observed_at = now_iso8601_utc();
        std::ostringstream json;
        json << "{\n";
        json << "  \"protocol_name\": \"" << json_escape(s_bridge_protocol_name) << "\",\n";
        json << "  \"protocol_version\": " << s_bridge_protocol_version << ",\n";
        json << "  \"event\": \"runtime_current_save_slot\",\n";
        json << "  \"status\": \"known\",\n";
        json << "  \"source\": \"new_game_staging\",\n";
        json << "  \"confidence\": \"station_of_awakening\",\n";
        json << "  \"save_slot\": \"AUTO\",\n";
        json << "  \"save_slot_label\": \"AUTO\",\n";
        json << "  \"slot_file\": \"kh3sv2/data/KHIII_slot0.bin\",\n";
        json << "  \"load_kind\": \"new_game_staging\",\n";
        json << "  \"observed_at_utc\": \"" << observed_at << "\",\n";
        json << "  \"ap_index_at_save\": 0,\n";
        json << "  \"last_applied_index_at_save\": 0,\n";
        json << "  \"replay_cutoff_index\": -1,\n";
        json << "  \"source_slot\": \"NEW_GAME\",\n";
        json << "  \"source_slot_ap_index\": 0,\n";
        json << "  \"probe_source\": \"" << json_escape(source) << "\",\n";
        json << "  \"runtime_context\": {\n";
        json << "    \"hook\": \"" << json_escape(snapshot.hook) << "\",\n";
        json << "    \"world\": \"" << json_escape(snapshot.world) << "\",\n";
        json << "    \"outer\": \"" << json_escape(snapshot.outer) << "\",\n";
        json << "    \"object\": \"" << json_escape(snapshot.object) << "\",\n";
        json << "    \"pawn_source\": \"" << json_escape(snapshot.pawn_source) << "\",\n";
        json << "    \"opened_at\": \"" << observed_at << "\"\n";
        json << "  }\n";
        json << "}\n";

        const std::string payload = json.str();
        const bool wrote_marker =
            write_text_file(*resolved_bridge_dir / channels::runtime_new_game_staging, payload);
        if (!wrote_marker)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Runtime new-game staging write failed source={} marker={}\n"),
                source, wrote_marker);
        }
        else
        {
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Runtime new-game staging wrote "
                                                "AUTO snapshot zero source={} pawn={}\n"),
                                            source, snapshot.object);
        }
    }

    auto RuntimePlayerContextService::Impl::maybe_write_new_game_staging_from_pawn_name(
        std::wstring_view source, std::wstring_view pawn_name) -> bool
    {
        if (!contains_new_game_prep_runtime_path(pawn_name))
        {
            return false;
        }
        auto snapshot = derive_runtime_context_from_object_full_name(
            pawn_name, STR("RuntimePlayablePawnContext"));
        if (!snapshot.has_value())
        {
            return false;
        }
        snapshot->pawn_source = std::wstring{source};
        write_new_game_staging_current_slot(source, *snapshot);
        return true;
    }

    auto RuntimePlayerContextService::Impl::try_write_new_game_staging_from_controller_pawn(
        UObject* controller, std::wstring_view source) -> bool
    {
        if (!controller || !kh3ap::is_uobject_valid_fast(controller))
        {
            return false;
        }
        const std::array<const wchar_t*, 3> properties{{
            STR("Pawn"),
            STR("AcknowledgedPawn"),
            STR("Character"),
        }};
        for (const wchar_t* const property_name : properties)
        {
            UObject* const pawn = read_object_property_in_chain(controller, property_name);
            if (!pawn || !kh3ap::is_uobject_valid_fast(pawn))
            {
                continue;
            }
            const std::wstring pawn_name = format_object_debug_name(pawn);
            const std::wstring class_name =
                pawn->GetClassPrivate() ? pawn->GetClassPrivate()->GetName() : std::wstring{};
            if (!is_validated_sora_pawn_name(pawn_name, class_name) ||
                !contains_new_game_prep_runtime_path(pawn_name))
            {
                continue;
            }
            const std::wstring pawn_source = std::format(STR("{}:{}"), source, property_name);
            const bool staged = maybe_write_new_game_staging_from_pawn_name(pawn_source, pawn_name);
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] StartingPlayerPawnCache staged StationOfAwakening new game "
                    "source={} staged={} pawn={}\n"),
                pawn_source, staged, pawn_name);
            return staged;
        }
        return false;
    }
} // namespace kh3ap
