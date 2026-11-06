#include "kh3ap/bridge/rewards/loot_payload_store.h"

#include "kh3ap/bridge/serialization/payload_serializer.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/pipe_transport.h"

#include <DynamicOutput/DynamicOutput.hpp>

#include <chrono>
#include <mutex>

namespace kh3ap
{
    namespace
    {
        std::mutex s_runtime_payload_emit_mutex{};
        std::wstring s_last_runtime_payload_signature{};
        uint64_t s_runtime_payload_emit_sequence{0};
        constexpr bool s_enable_chest_timing_logging_local = false;

        auto elapsed_us(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) -> uint64_t
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        }
    }

    auto RuntimeLootPayloadStore::write(
            const std::vector<std::filesystem::path>& output_paths,
            const RuntimeLootPayload& payload) -> bool
    {
        const auto write_start = std::chrono::steady_clock::now();
        const std::string content = RuntimePayloadSerializer::serialize_loot_payload(payload);
        const auto serialize_end = std::chrono::steady_clock::now();
        bool wrote_any = false;
        uint64_t path_write_total_us = 0;

        for (const auto& path : output_paths)
        {
            std::error_code ec{};
            if (path.has_parent_path() && !pipe::manages(path))
            {
                std::filesystem::create_directories(path.parent_path(), ec);
            }

            const auto path_write_start = std::chrono::steady_clock::now();
            if (!write_text_file(path, content))
            {
                RC::Output::send<RC::LogLevel::Error>(
                        STR("[KH3APProbeCpp] Failed to write output path {}\n"),
                        path.wstring());
                path_write_total_us += elapsed_us(path_write_start, std::chrono::steady_clock::now());
                continue;
            }
            path_write_total_us += elapsed_us(path_write_start, std::chrono::steady_clock::now());

            wrote_any = true;
        }

        if constexpr (s_enable_chest_timing_logging_local)
        {
            if (payload.type == STR("treasure"))
            {
                const auto write_end = std::chrono::steady_clock::now();
                RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ChestTiming payload_write status={} bridge_key={} paths={} bytes={} serialize_us={} file_write_us={} total_us={}\n"),
                        wrote_any ? STR("wrote") : STR("not_written"),
                        payload.bridge_key,
                        static_cast<uint64_t>(output_paths.size()),
                        static_cast<uint64_t>(content.size()),
                        elapsed_us(write_start, serialize_end),
                        path_write_total_us,
                        elapsed_us(write_start, write_end));
            }
        }

        return wrote_any;
    }

    auto RuntimeLootPayloadStore::emit_once(
            const std::vector<std::filesystem::path>& output_paths,
            const RuntimeLootPayload& payload) -> bool
    {
        const auto emit_start = std::chrono::steady_clock::now();
        const std::wstring emit_signature =
                payload.bridge_key + STR("|") +
                payload.token_value + STR("|") +
                payload.opened_at + STR("|") +
                payload.actor_name;
        const auto signature_end = std::chrono::steady_clock::now();
        std::lock_guard emit_lock(s_runtime_payload_emit_mutex);
        const auto lock_end = std::chrono::steady_clock::now();
        if (s_last_runtime_payload_signature == emit_signature)
        {
            if constexpr (s_enable_chest_timing_logging_local)
            {
                if (payload.type == STR("treasure"))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                            STR("[KH3APProbeCpp] ChestTiming emit_once status=duplicate bridge_key={} signature_us={} lock_us={} total_us={}\n"),
                            payload.bridge_key,
                            elapsed_us(emit_start, signature_end),
                            elapsed_us(signature_end, lock_end),
                            elapsed_us(emit_start, std::chrono::steady_clock::now()));
                }
            }
            return false;
        }

        RuntimeLootPayload payload_to_write = payload;
        payload_to_write.observation_seq = ++s_runtime_payload_emit_sequence;
        const bool wrote = write(output_paths, payload_to_write);
        const auto write_end = std::chrono::steady_clock::now();
        if (wrote)
        {
            s_last_runtime_payload_signature = emit_signature;
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Emitted runtime payload seq={} | type={} | bridge_key={} | token={} | resolution_kind={} | grant_context={} | encounter_hint={}\n"),
                    payload_to_write.observation_seq,
                    payload_to_write.type,
                    payload_to_write.bridge_key,
                    payload_to_write.token_value,
                    payload_to_write.resolution_kind,
                    payload_to_write.grant_context.empty() ? std::wstring{STR("<none>")} : payload_to_write.grant_context,
                    payload_to_write.encounter_hint.empty() ? std::wstring{STR("<none>")} : payload_to_write.encounter_hint);
        }
        if constexpr (s_enable_chest_timing_logging_local)
        {
            if (payload.type == STR("treasure"))
            {
                RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ChestTiming emit_once status={} bridge_key={} signature_us={} lock_us={} write_us={} total_us={}\n"),
                        wrote ? STR("wrote") : STR("not_written"),
                        payload.bridge_key,
                        elapsed_us(emit_start, signature_end),
                        elapsed_us(signature_end, lock_end),
                        elapsed_us(lock_end, write_end),
                        elapsed_us(emit_start, write_end));
            }
        }
        return wrote;
    }

    auto RuntimeLootPayloadStore::should_emit(
            const RuntimeLootPayload& payload,
            const std::optional<RuntimeContextSnapshot>& runtime_context,
            bool debug_logging) -> bool
    {
        if (payload.type == STR("vbonus"))
        {
            const bool compact_resolved =
                    payload.resolution_kind == STR("compact_direct") ||
                    payload.resolution_kind == STR("compact_fname") ||
                    payload.resolution_kind == STR("event_array_removed");
            if (!compact_resolved)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Payload suppress bridge_key={} token={} reason=non_compact_vbonus resolution_kind={} hook={} world={} outer={}\n"),
                        payload.bridge_key,
                        payload.token_value,
                        payload.resolution_kind.empty() ? std::wstring{STR("<none>")} : payload.resolution_kind,
                        runtime_context.has_value() ? runtime_context->hook : std::wstring{STR("<none>")},
                        runtime_context.has_value() ? runtime_context->world : std::wstring{STR("<none>")},
                        runtime_context.has_value() ? runtime_context->outer : std::wstring{STR("<none>")});
                return false;
            }
        }

        if (payload.type == STR("vbonus") && payload.source_id == STR("Vbonus_001"))
        {
            if (debug_logging)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Payload suppress bridge_key={} token={} reason=ambiguous_vbonus_001 hook={} world={} outer={}\n"),
                        payload.bridge_key,
                        payload.token_value,
                        runtime_context.has_value() ? runtime_context->hook : std::wstring{STR("<none>")},
                        runtime_context.has_value() ? runtime_context->world : std::wstring{STR("<none>")},
                        runtime_context.has_value() ? runtime_context->outer : std::wstring{STR("<none>")});
            }
            return false;
        }

        return true;
    }
}
