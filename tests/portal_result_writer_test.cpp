#include "kh3ap/features/world/portals/portal_result_writer.h"
#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/utilities.h"

#include <cassert>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace
{
    using Publication = std::pair<std::filesystem::path, std::string>;
    std::mutex publications_mutex;
    std::condition_variable published;
    std::vector<Publication> publications;

    auto wait_for_publications(size_t count) -> std::vector<Publication>
    {
        std::unique_lock lock{publications_mutex};
        const bool complete = published.wait_for(lock, std::chrono::seconds(2), [count] {
            return publications.size() >= count;
        });
        assert(complete);
        return publications;
    }
}

// Exercise the production serializer and worker with an in-memory bridge.
namespace kh3ap::pipe
{
    auto manages(const std::filesystem::path&) -> bool
    {
        return true;
    }

    auto read(const std::filesystem::path&) -> std::optional<std::string>
    {
        assert(false && "portal result publication must not read the bridge");
        return std::nullopt;
    }

    auto write(const std::filesystem::path& path, std::string_view text) -> bool
    {
        {
            std::lock_guard lock{publications_mutex};
            publications.emplace_back(path, text);
        }
        published.notify_all();
        return true;
    }
}

int main()
{
    // Every JSON control character must survive a round trip, including NUL.
    std::wstring control_text;
    for (wchar_t character = 0; character < 0x20; ++character)
    {
        control_text.push_back(character);
    }
    control_text += L"quotes \" slash \\ Unicode \u65e5\U0001f600";
    kh3ap::json::Value control_json;
    assert(kh3ap::json::parse(control_json,
        "{\"text\":\"" + kh3ap::json_escape(control_text) + "\"}"));
    assert(control_json["text"] == kh3ap::to_utf8(control_text));

    kh3ap::PortalSpawnResult result{};
    result.result_path = L"portal_result.json";
    result.events_path = L"portal_events.jsonl";
    result.protocol_name = L"kh3_ap_file_bridge";
    result.protocol_version = 1;
    result.status = L"applied";
    result.source_name = L"manual";
    result.request_id = L"portal-1";
    result.pawn_name = L"Sora \"pawn\"\n";
    result.pawn_address = 0x1234;
    result.spawned_actor_address = 0xabcd;
    result.stable_ms = 250;
    result.x = 1.25;
    result.y = -2.5;
    result.z = 3.0;

    kh3ap::write_portal_spawn_result(result);
    const auto synchronous = wait_for_publications(2);
    assert(synchronous[0].first == result.result_path);
    assert(synchronous[1].first == result.events_path);
    assert(synchronous[0].second == synchronous[1].second);
    auto expected = kh3ap::json::Value::parse(synchronous[0].second);
    assert(expected["schema"] == "kh3ap.runtime_portal_spawn_result.v1");
    assert(expected["event"] == "runtime_portal_spawn");
    assert(expected["pawn"] == "Sora \"pawn\"\n");
    assert(expected["pawn_address"] == "0x1234");
    assert(expected["spawned_actor_address"] == "0xabcd");
    assert(expected["pawn_stable_ms"] == 250);
    assert(expected["location"]["x"] == 1.25);
    assert(expected["location"]["y"] == -2.5);
    assert(expected["location"]["z"] == 3.0);
    assert(expected["observed_at_utc"].is_string());
    assert(!expected.contains("stage") && !expected.contains("reason"));
    assert(synchronous[0].second.find("\"x\":1.250,\"y\":-2.500,\"z\":3.000") !=
           std::string::npos);

    result.stage = L"spawn";
    result.reason = L"ready";
    kh3ap::initialize_portal_result_writer();
    kh3ap::enqueue_portal_spawn_result(result);
    result.reason = L"changed after enqueue";
    const auto asynchronous = wait_for_publications(4);
    assert(asynchronous[2].first == result.result_path);
    assert(asynchronous[3].first == result.events_path);
    assert(asynchronous[2].second == asynchronous[3].second);
    auto actual = kh3ap::json::Value::parse(asynchronous[2].second);
    actual.erase("observed_at_utc");
    expected.erase("observed_at_utc");
    expected["stage"] = "spawn";
    expected["reason"] = "ready";
    assert(actual == expected);
}
