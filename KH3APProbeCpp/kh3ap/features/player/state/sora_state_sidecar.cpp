#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/session/deathlink.h"
#include "kh3ap/features/player/state/sora_state_sidecar.h"

#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"

#include <UnrealDef.hpp>

#include <format>
#include <sstream>

namespace kh3ap
{
    namespace
    {
        auto map_root_or_none(std::wstring_view map_root) -> std::wstring
        {
            return map_root.empty() ? std::wstring{STR("<none>")} : std::wstring{map_root};
        }

        auto runtime_context_signature(const RuntimeSoraStateSidecarDeps& deps) -> std::wstring
        {
            return deps.runtime_context.empty()
                    ? std::wstring{STR("context=<none>")}
                    : std::wstring{deps.runtime_context};
        }

        auto is_safe_playable_runtime_context_snapshot(const RuntimeContextSnapshot& snapshot) -> bool
        {
            return snapshot.hook == STR("RuntimePlayablePawnContext") &&
                   !snapshot.world.empty() &&
                   !snapshot.outer.empty() &&
                   !snapshot.opened_at.empty() &&
                   !snapshot.object.empty() &&
                   snapshot.object.find(STR("real=false")) == std::wstring::npos;
        }

        auto maybe_refresh_runtime_context_for_safe_sora_state(
                const RuntimeSoraStateSidecarDeps& deps,
                RuntimeSoraStateSafety state_safety) -> void
        {
            // Encounter cameras can replace the published context during Retry.
            // Recover from the verified current pawn, never the camera context.
            if (state_safety == RuntimeSoraStateSafety::Ok && deathlink::view()->recovery_pending &&
                RuntimeObjectResolver::is_sora_ability_pawn_candidate(deps.pawn))
            {
                auto snapshot = derive_runtime_context_from_object_full_name(
                        format_object_debug_name(deps.pawn), STR("RuntimePlayablePawnContext"));
                if (snapshot)
                {
                    snapshot->pawn_source = STR("DeathLinkRecovery:current_player_pawn");
                    write_runtime_context_file(deps.bridge_dir, *snapshot);
                    return;
                }
            }
            if (state_safety != RuntimeSoraStateSafety::Ok ||
                deps.special_battle_mode_active ||
                !deps.runtime_context_snapshot ||
                !is_safe_playable_runtime_context_snapshot(*deps.runtime_context_snapshot))
            {
                return;
            }

            write_runtime_context_file(deps.bridge_dir, *deps.runtime_context_snapshot);
        }

        auto append_special_battle_fields(std::ostringstream& json, const RuntimeSoraStateSidecarDeps& deps) -> void
        {
            json << ",\"special_battle_mode_active\":"
                 << (deps.special_battle_mode_active ? "true" : "false");
            json << ",\"special_battle_mode_source\":\""
                 << json_escape(deps.special_battle_mode_source) << "\"";
            json << ",\"special_battle_mode_details\":\""
                 << json_escape(deps.special_battle_mode_details) << "\"";
            json << ",\"special_battle_mode_map_root\":\""
                 << json_escape(deps.special_battle_mode_map_root) << "\"";
        }

        auto write_sidecar_payload(const std::filesystem::path& bridge_dir, const std::string& payload, bool event_log_enabled)
                -> void
        {
            write_text_file(bridge_dir / channels::runtime_sora_state, payload);
            if (event_log_enabled)
            {
                append_text_file(bridge_dir / "runtime_sora_state_events.jsonl", payload);
            }
        }
    }

    auto write_runtime_sora_state_sidecar_on_change(
            std::wstring& last_signature,
            const RuntimeSoraStateSidecarDeps& deps) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        if (!deps.pawn)
        {
            const std::wstring signature = std::format(
                    STR("unavailable|special_battle_mode={}|special_battle_mode_map_root={}|{}"),
                    deps.special_battle_mode_active ? STR("true") : STR("false"),
                    map_root_or_none(deps.special_battle_mode_map_root),
                    runtime_context_signature(deps));
            if (signature == last_signature)
            {
                return;
            }
            last_signature = signature;

            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.runtime_sora_state.v1\"";
            json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
            json << ",\"protocol_version\":" << deps.protocol_version;
            json << ",\"event\":\"runtime_sora_state\"";
            json << ",\"status\":\"unavailable\"";
            json << ",\"reason\":\"missing_or_invalid_cached_pawn\"";
            json << ",\"state_key\":\"unavailable\"";
            json << ",\"safety\":\"not_ok\"";
            append_special_battle_fields(json, deps);
            json << ",\"observed_at_utc\":\"" << now_iso8601_utc() << "\"";
            json << ",\"runtime_context\":\"" << json_escape(deps.runtime_context) << "\"";
            json << "}\n";
            write_sidecar_payload(*deps.bridge_dir, json.str(), deps.event_log_enabled);
            return;
        }

        const auto& cache = deps.current_state_cache;
        const std::wstring_view state_source = cache.state_source.empty()
                ? std::wstring_view{STR("current_state_write_polyhook")}
                : std::wstring_view{cache.state_source};
        std::wstring state_class = cache.current_state_valid ? cache.state_class : std::wstring{};
        const std::optional<uint8_t> current_state_id =
                cache.current_state_valid ? cache.current_state_id : std::nullopt;
        const bool current_state_cache_ready =
                cache.state_comp_valid &&
                cache.current_state_valid &&
                cache.pawn_address == reinterpret_cast<uintptr_t>(deps.pawn) &&
                (current_state_id.has_value() || !state_class.empty());
        if (!current_state_cache_ready)
        {
            const std::wstring signature = std::format(
                    STR("unavailable|reason=current_state_polyhook_pending|special_battle_mode={}|special_battle_mode_map_root={}|{}"),
                    deps.special_battle_mode_active ? STR("true") : STR("false"),
                    map_root_or_none(deps.special_battle_mode_map_root),
                    runtime_context_signature(deps));
            if (signature == last_signature)
            {
                return;
            }
            last_signature = signature;

            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.runtime_sora_state.v1\"";
            json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
            json << ",\"protocol_version\":" << deps.protocol_version;
            json << ",\"event\":\"runtime_sora_state\"";
            json << ",\"status\":\"unavailable\"";
            json << ",\"reason\":\"current_state_polyhook_pending\"";
            json << ",\"source\":\"" << json_escape(state_source) << "\"";
            json << ",\"state_key\":\"" << json_escape(signature) << "\"";
            json << ",\"safety\":\"not_ok\"";
            append_special_battle_fields(json, deps);
            json << ",\"polyhook_installed\":"
                 << (cache.polyhook_installed ? "true" : "false");
            json << ",\"polyhook_target\":\"0x" << std::hex
                 << cache.polyhook_target << std::dec << "\"";
            json << ",\"last_polyhook_event_index\":" << cache.event_index;
            json << ",\"observed_at_utc\":\"" << now_iso8601_utc() << "\"";
            json << ",\"runtime_context\":\"" << json_escape(deps.runtime_context) << "\"";
            json << "}\n";
            write_sidecar_payload(*deps.bridge_dir, json.str(), deps.event_log_enabled);
            return;
        }

        const RuntimeSoraStateCatalogEntry* state_catalog_entry =
                current_state_id.has_value() ? lookup_runtime_sora_state_by_id(*current_state_id) : nullptr;
        if (state_catalog_entry)
        {
            state_class = std::wstring{state_catalog_entry->class_name};
        }
        else if (!state_class.empty())
        {
            state_catalog_entry = lookup_runtime_sora_state_by_class_name(state_class);
        }
        const RuntimeSoraStateSafety state_safety =
                state_catalog_entry ? state_catalog_entry->safety : RuntimeSoraStateSafety::NotOk;
        const std::wstring_view state_safety_label = runtime_sora_state_safety_label(state_safety);

        const std::wstring signature = std::format(
                STR("known|{}|{}|special_battle_mode={}|special_battle_mode_map_root={}|{}"),
                state_class.empty() ? std::wstring{STR("<none>")} : state_class,
                current_state_id.has_value() ? std::to_wstring(*current_state_id) : std::wstring{STR("<none>")},
                deps.special_battle_mode_active ? STR("true") : STR("false"),
                map_root_or_none(deps.special_battle_mode_map_root),
                runtime_context_signature(deps));
        if (signature == last_signature)
        {
            return;
        }
        last_signature = signature;
        if (deps.activate_blocking_ui_from_sora_state)
        {
            deps.activate_blocking_ui_from_sora_state(state_class, current_state_id);
        }

        std::ostringstream json;
        json << "{";
        json << "\"schema\":\"kh3ap.runtime_sora_state.v1\"";
        json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
        json << ",\"protocol_version\":" << deps.protocol_version;
        json << ",\"event\":\"runtime_sora_state\"";
        json << ",\"status\":\"known\"";
        json << ",\"observed_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"source\":\"" << json_escape(state_source) << "\"";
        json << ",\"polyhook_event_index\":" << cache.event_index;
        json << ",\"state_class\":\"" << json_escape(state_class) << "\"";
        json << ",\"state_key\":\"" << json_escape(signature) << "\"";
        json << ",\"safety\":\"" << json_escape(state_safety_label) << "\"";
        append_special_battle_fields(json, deps);
        json << ",\"catalog_match\":" << (state_catalog_entry ? "true" : "false");
        if (state_catalog_entry)
        {
            json << ",\"catalog_note\":\"" << json_escape(state_catalog_entry->note) << "\"";
        }
        json << ",\"current_state_id\":";
        if (current_state_id.has_value())
        {
            json << static_cast<int>(*current_state_id);
        }
        else
        {
            json << "null";
        }
        json << ",\"current_state_name\":\"\"";
        json << ",\"current_state\":\"" << json_escape(state_class) << "\"";
        json << ",\"current_state_pawn\":\"<not_sampled>\"";
        json << ",\"current_state_pawn_is_sora_candidate\":null";
        json << ",\"sora_pawn\":\"0x" << std::hex
             << reinterpret_cast<uintptr_t>(deps.pawn) << std::dec << "\"";
        json << ",\"state_comp\":\"0x" << std::hex
             << cache.state_comp_address << std::dec << "\"";
        json << ",\"runtime_context\":\"" << json_escape(deps.runtime_context) << "\"";
        json << "}\n";
        maybe_refresh_runtime_context_for_safe_sora_state(deps, state_safety);
        write_sidecar_payload(*deps.bridge_dir, json.str(), deps.event_log_enabled);
    }
}
