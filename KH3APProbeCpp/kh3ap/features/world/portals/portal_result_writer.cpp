#include "kh3ap/features/world/portals/portal_result_writer.h"
#include "kh3ap/bridge/utilities.h"

#include <condition_variable>
#include <deque>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace kh3ap
{
    namespace
    {
        auto append_json_string_field(std::ostringstream& json, std::string_view key, std::wstring_view value) -> void
        {
            json << ",\"" << key << "\":\"" << json_escape(value) << "\"";
        }

        auto append_json_number_field(std::ostringstream& json, std::string_view key, double value) -> void
        {
            json << ",\"" << key << "\":" << std::fixed << std::setprecision(3) << value;
        }

        auto serialize_portal_spawn_result(const PortalSpawnResult& result) -> std::string
        {
            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.runtime_portal_spawn_result.v1\"";
            json << ",\"protocol_name\":\"" << json_escape(result.protocol_name) << "\"";
            json << ",\"protocol_version\":" << result.protocol_version;
            json << ",\"event\":\"runtime_portal_spawn\"";
            if (!result.stage.empty())
            {
                append_json_string_field(json, "stage", result.stage);
            }
            append_json_string_field(json, "status", result.status);
            if (!result.reason.empty())
            {
                append_json_string_field(json, "reason", result.reason);
            }
            append_json_string_field(json, "source", result.source_name);
            append_json_string_field(json, "request_id", result.request_id);
            append_json_string_field(json, "build_stamp", result.build_stamp);
            const std::string observed_at = now_iso8601_utc();
            append_json_string_field(json, "observed_at_utc", std::wstring{observed_at.begin(), observed_at.end()});
            append_json_string_field(json, "map_path", result.map_path);
            append_json_string_field(json, "context_key", result.context_key);
            append_json_string_field(json, "runtime_context", result.runtime_context);
            append_json_string_field(json, "pawn", result.pawn_name);
            append_json_string_field(json, "spawned_actor", result.spawned_actor_name);
            append_json_string_field(json, "portal_class_path", result.portal_class_path);
            append_json_string_field(json, "portal_blueprint_path", result.portal_blueprint_path);
            append_json_string_field(json, "class_resolution_source", result.class_resolution_source);
            json << ",\"pawn_address\":\"0x" << std::hex << result.pawn_address << std::dec << "\"";
            json << ",\"spawned_actor_address\":\"0x" << std::hex << result.spawned_actor_address << std::dec << "\"";
            json << ",\"pawn_stable_ms\":" << result.stable_ms;
            append_json_number_field(json, "x", result.x);
            append_json_number_field(json, "y", result.y);
            append_json_number_field(json, "z", result.z);
            json << ",\"location\":{";
            json << "\"x\":" << std::fixed << std::setprecision(3) << result.x;
            json << ",\"y\":" << std::fixed << std::setprecision(3) << result.y;
            json << ",\"z\":" << std::fixed << std::setprecision(3) << result.z;
            json << "}";
            json << "}\n";
            return json.str();
        }

        class PortalResultWriter
        {
        public:
            PortalResultWriter()
                : m_worker([this](std::stop_token stop_token) { run(stop_token); })
            {
            }

            ~PortalResultWriter()
            {
                m_worker.request_stop();
                m_ready.notify_all();
            }

            PortalResultWriter(const PortalResultWriter&) = delete;
            auto operator=(const PortalResultWriter&) -> PortalResultWriter& = delete;

            auto enqueue(PortalSpawnResult result) -> void
            {
                {
                    std::lock_guard lock{m_mutex};
                    constexpr size_t max_pending_results = 16;
                    if (m_pending.size() >= max_pending_results)
                    {
                        m_pending.pop_front();
                    }
                    m_pending.push_back(std::move(result));
                }
                m_ready.notify_one();
            }

        private:
            auto run(std::stop_token stop_token) -> void
            {
                while (true)
                {
                    PortalSpawnResult result{};
                    {
                        std::unique_lock lock{m_mutex};
                        m_ready.wait(lock, [&]() {
                            return stop_token.stop_requested() || !m_pending.empty();
                        });
                        if (m_pending.empty())
                        {
                            if (stop_token.stop_requested())
                            {
                                return;
                            }
                            continue;
                        }
                        result = std::move(m_pending.front());
                        m_pending.pop_front();
                    }

                    write_portal_spawn_result(result);
                }
            }

            std::mutex m_mutex{};
            std::condition_variable m_ready{};
            std::deque<PortalSpawnResult> m_pending{};
            std::jthread m_worker{};
        };

        auto portal_result_writer() -> PortalResultWriter&
        {
            static PortalResultWriter writer{};
            return writer;
        }
    }

    auto write_portal_spawn_result(const PortalSpawnResult& result) -> void
    {
        const std::string payload = serialize_portal_spawn_result(result);
        write_text_file(result.result_path, payload);
        append_text_file(result.events_path, payload);
    }

    auto enqueue_portal_spawn_result(PortalSpawnResult result) -> void
    {
        portal_result_writer().enqueue(std::move(result));
    }

    auto initialize_portal_result_writer() -> void
    {
        static_cast<void>(portal_result_writer());
    }
}
